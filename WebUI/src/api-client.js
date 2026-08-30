// In production the API is same-origin (relative URLs), so the UI works
// identically over http://vybes.local and https://vybes.local - hardcoding
// a scheme would break one of them (mixed content under HTTPS).
// In dev, VITE_API_BASE_URL (e.g. in a .env.development.local) overrides the
// default mock-server hostname.
const API_BASE_URL = import.meta.env.DEV
  ? (import.meta.env.VITE_API_BASE_URL || 'http://vybes-mock.local')
  : ''

// Ceiling on a single queued request, so one stall cannot freeze every
// request behind it. See the note on request() for why they are queued.
const REQUEST_TIMEOUT_MS = 15000

// A dropped connection is retried; see _performRequest for why. The delays
// only have to outlast a transient heap dip on the device, which is
// milliseconds, so they are short enough to stay invisible.
const TRANSPORT_RETRY_DELAYS_MS = [250, 750]

// Methods safe to replay when we never learned the outcome. GET/PUT/DELETE
// in this API are all full-state operations, so a replay either repeats the
// same write or reads again. POST is not: /preset creates, /restore reboots,
// /recorder/record/start begins a new file.
const REPLAYABLE_METHODS = new Set(['GET', 'PUT', 'DELETE', 'HEAD'])

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms))

/*
 * Vybes DSP API Client - Enhanced Version
 * A comprehensive JavaScript client for interacting with the Vybes DSP system
 */
class VybesAPI {
  constructor() {
    this.baseUrl = API_BASE_URL;
    this.socket = null;

    // Tail of the request queue - see request(). Always a settled-or-pending
    // promise that never rejects, so the chain cannot be broken by a failure.
    this.requestChain = Promise.resolve();

    // Live-update plumbing: one shared socket, many listeners, and
    // automatic reconnection with exponential backoff.
    this.messageListeners = new Set();
    this.statusListeners = new Set();
    this.connectionState = 'disconnected'; // 'disconnected' | 'connecting' | 'connected'
    this.reconnectTimer = null;
    this.reconnectDelay = 1000;
    // Latest message handed to sendLiveMessage while the socket wasn't
    // open yet; flushed as soon as it opens.
    this.pendingLiveMessage = null;
  }

  get isWebSocketConnected() {
    return this.socket && this.socket.readyState === WebSocket.OPEN;
  }

  /**
   * Make an HTTP request, queued so only one is ever in flight.
   *
   * The device's HTTPS listener affords 2 TLS sockets at ~50KB of internal
   * heap each (see ESP/esp-web-server/web_server.cpp), and the live-updates
   * websocket permanently holds one of them - it carries 20 state-change
   * broadcast types, so it cannot be opened lazily without the UI going stale
   * whenever the IR remote or front button changes something. That leaves
   * exactly ONE socket for REST.
   *
   * Nothing here used to respect that. There is no Promise.all in the app,
   * but a view mount has five or six components each loading independently in
   * the same tick, so the browser opened a connection per request, the
   * listener evicted the least-recently-used one (lru_purge_enable), and the
   * evicted connection's in-flight request failed. That was the sporadic
   * "failed API request" seen whenever the websocket was also connected.
   *
   * Queueing fixes it at the source: one request in flight means the browser
   * reuses a single keep-alive connection and never opens a second. Requests
   * run back-to-back rather than overlapping - about 19ms each on a reused
   * connection, so a six-request mount costs ~120ms instead of ~40ms, which
   * is not perceptible. If the per-connection TLS cost ever comes down (an
   * esp-idf-as-component build could set MBEDTLS_ASYMMETRIC_CONTENT_LEN) this
   * stays correct, it just stops being load-bearing.
   *
   * @param {string} method - HTTP method
   * @param {string} endpoint - API endpoint
   * @param {Object} body - Request body for POST requests
   * @returns {Promise<Object>} Response data
   */
  request(method, endpoint, body = null, isFormData = false) {
    // The retry loop lives inside the queue slot, so a replay reuses the same
    // keep-alive connection instead of racing a second one into the listener.
    const run = () => this._requestWithRetry(method, endpoint, body, isFormData);
    // Chained on settlement rather than success: one rejection must not stop
    // everything queued behind it.
    const result = this.requestChain.then(run, run);
    this.requestChain = result.then(() => undefined, () => undefined);
    return result;
  }

  /**
   * Replay a request whose outcome we never learned.
   *
   * fetch() rejects with a bare TypeError ("Load failed" in Safari, "Failed to
   * fetch" in Chrome) when the connection dies before a response arrives. That
   * says nothing about whether the device acted: its handlers commit the change
   * and only then reply, so a drop after the commit looks identical to one
   * before it. The analyzer made this routine - it streams RTA frames the whole
   * time it is open, and that churn dips the device's largest free block below
   * the 16KB a TLS handshake needs (see ESP/esp-web-server/health.cpp). A
   * request unlucky enough to land inside the dip dies, and an EQ apply that
   * had already been written reported itself as a failure.
   *
   * Only genuine transport failures are replayed. An HTTP error status is the
   * device's considered answer, and a timeout has already spent its 15s, so
   * neither is retried; a drop fails in milliseconds, which keeps the added
   * head-of-line blocking bounded.
   */
  async _requestWithRetry(method, endpoint, body, isFormData) {
    const replayable = REPLAYABLE_METHODS.has(method.toUpperCase()) && !isFormData;
    for (let attempt = 0; ; attempt++) {
      try {
        return await this._performRequest(method, endpoint, body, isFormData);
      } catch (error) {
        // Per the fetch spec a network error rejects with a TypeError, which
        // separates a dropped connection from a bad JSON body on a 2xx.
        const transportFailure = error instanceof TypeError;
        if (!replayable || !transportFailure || attempt >= TRANSPORT_RETRY_DELAYS_MS.length) {
          throw error;
        }
        console.warn(
          `API connection dropped, retrying (${attempt + 1}/${TRANSPORT_RETRY_DELAYS_MS.length}): ` +
          `${method} ${endpoint}`);
        await sleep(TRANSPORT_RETRY_DELAYS_MS[attempt]);
      }
    }
  }

  async _performRequest(method, endpoint, body, isFormData) {
    const url = this.baseUrl + endpoint;
    const config = {
      method: method.toUpperCase(),
      headers: {},
    };

    if (body) {
      if (isFormData) {
        config.body = body;
      } else {
        config.headers['Content-Type'] = 'application/json';
        config.body = JSON.stringify(body);
      }
    }

    // Bounds the head-of-line blocking the queue introduces: before it, a
    // stalled request only froze its own caller. Uploads opt out - /restore
    // and FIR files are legitimately slow, and the goal is to avoid a second
    // connection, not to hurry them.
    const controller = new AbortController();
    config.signal = controller.signal;
    const timer = isFormData
      ? null
      : setTimeout(() => controller.abort(), REQUEST_TIMEOUT_MS);

    try {
      const response = await fetch(url, config);
      const text = await response.text();

      if (!response.ok) {
        // Attach the status and parsed body so callers can act on
        // structured rejections (e.g. 409 lock/floor/pool errors)
        let errorBody = null;
        try { errorBody = text ? JSON.parse(text) : null; } catch (e) { /* not JSON */ }
        const message = (errorBody && errorBody.error) || `HTTP ${response.status}: ${response.statusText}`;
        const error = new Error(message);
        error.status = response.status;
        error.body = errorBody;
        throw error;
      }

      // Handle empty responses
      return text ? JSON.parse(text) : {};
    } catch (error) {
      if (error.name === 'AbortError') {
        const timeout = new Error(
          `Request timed out after ${REQUEST_TIMEOUT_MS}ms: ${method} ${endpoint}`);
        timeout.timeout = true;
        console.error(`API request timed out: ${method} ${endpoint}`);
        throw timeout;
      }
      console.error(`API request failed: ${method} ${endpoint}`, error);
      throw error;
    } finally {
      if (timer) clearTimeout(timer);
    }
  }

  // ===== SYSTEM CONTROLS =====

  /**
   * Mute on/off
   * @param {boolean} state - true for on, false for off
   */
  async setMute(state) {
    const stateStr = state ? 'on' : 'off';
    return this.request('PUT', `/mute?state=${stateStr}`);
  }

  /**
   * Set mute percentage
   * @param {number} percent - Mute percentage (0-100)
   */
  async setMutePercent(percent) {
    if (percent < 0 || percent > 100) {
      throw new Error('Mute percent must be between 0 and 100');
    }
    return this.request('PUT', `/mute/percent?percent=${percent}`);
  }

  /**
   * Set active preset
   * @param {string} name - Preset name to activate
   */
  async setActivePreset(name) {
    return this.request('PUT', `/preset/active?name=${encodeURIComponent(name)}`);
  }

  /**
   * Get system status
   * @returns {Promise<Object>} Current system status including all settings
   */
  async getStatus() {
    return this.request('GET', '/status');
  }

  // ===== TONE GENERATION =====

  /**
   * Generate tone
   * @param {number} frequency - Frequency (20-20000 Hz)
   * @param {number} volume - Volume (1-100)
   */
  async generateTone(frequency, volume) {
    if (frequency < 20 || frequency > 20000) {
      throw new Error('Frequency must be between 20 and 20000 Hz');
    }
    if (volume < 1 || volume > 100) {
      throw new Error('Volume must be between 1 and 100');
    }
    return this.request('PUT', `/generate/tone?frequency=${frequency}&volume=${volume}`);
  }

  /**
   * Stop tone generation
   */
  async stopTone() {
    return this.request('PUT', '/generate/tone/stop');
  }

  /**
   * Generate pink noise
   * @param {number} volume - Volume (0-100, 0 turns off)
   */
  async generateNoise(volume) {
    if (volume < 0 || volume > 100) {
      throw new Error('Volume must be between 0 and 100');
    }
    return this.request('PUT', `/noise?level=${volume}`);
  }

  // ===== AUTO DELAY ALIGNMENT PROBE =====

  /**
   * Start a delay probe: the device chirps every enabled output of the
   * active preset in sequence. Resolves with the chirp schedule
   * (sampleRate, preRollSamples, spacingSamples, chirpSamples, tailSamples,
   * fadeSamples, f0, f1, order) that delay-align.js correlates against.
   * Progress arrives as probeEvent live-update messages.
   * @param {number} level - Probe loudness 0-100 (independent of volume)
   */
  async startDelayProbe(level = 50) {
    if (level < 0 || level > 100) {
      throw new Error('Level must be between 0 and 100');
    }
    return this.request('PUT', `/probe/delay/start?level=${level}`);
  }

  /** Cancel a running delay probe */
  async stopDelayProbe() {
    return this.request('PUT', '/probe/delay/stop');
  }

  // ===== AUTO-FIR MEASUREMENT SWEEP =====
  // docs/AUTO_FIR_CONTRACTS.md: the response here only echoes the request -
  // the authoritative schedule (preRoll/spacing/fade, on the Teensy's own
  // clock) arrives as a "SWEEP START ..." probeEvent on the same live-updates
  // channel startDelayProbe uses, parsed by fir-wizard.js's
  // parseSweepStartLine.
  /**
   * Start a measurement sweep: solos each enabled output of the active
   * preset in turn, `passes` exponential sweeps each (back to back, not
   * reversed - a pass-to-pass drift/consistency check on the same output).
   * @param {Object} [opts]
   * @param {number} [opts.level=50]           playback level, 0-100
   * @param {number} [opts.f0=20]               sweep start frequency, Hz
   * @param {number} [opts.f1=20000]            sweep end frequency, Hz
   * @param {number} [opts.chirpSamples=131072] sweep length, device samples
   * @param {number} [opts.passes=2]
   */
  async startSweepProbe(opts = {}) {
    const { level = 50, f0 = 20, f1 = 20000, chirpSamples = 131072, passes = 2 } = opts;
    const params = new URLSearchParams({
      level: String(level), f0: String(f0), f1: String(f1),
      chirpSamples: String(chirpSamples), passes: String(passes),
    });
    return this.request('PUT', `/probe/sweep/start?${params.toString()}`);
  }

  /** Cancel a running measurement sweep (shares stopDelayProbe's device command). */
  async stopSweepProbe() {
    return this.request('PUT', '/probe/sweep/stop');
  }

  // ===== FIR FILE UPLOAD / DELETE =====

  /**
   * Upload a raw FIR kernel: little-endian float32 taps, no header.
   * @param {string} name           e.g. "a3-woofer-l.bin"
   * @param {ArrayBuffer} arrayBuffer
   * @returns {Promise<{name:string, size:number, taps:number}>}
   */
  async uploadFir(name, arrayBuffer) {
    return this.request('POST', `/fir/upload?name=${encodeURIComponent(name)}`, arrayBuffer, true);
  }

  /** Delete a FIR file from the SD card. 409 {error:'referenced', presets:[...]} if a preset still uses it. */
  async deleteFirFile(name) {
    return this.request('DELETE', `/fir/files?name=${encodeURIComponent(name)}`);
  }

  // ===== PRESET MANAGEMENT =====

  /**
   * Get all presets
   * @returns {Promise<Array>} Array of preset objects with name and isCurrent
   */
  async getPresets() {
    return this.request('GET', '/presets');
  }

  /**
   * Get the list of FIR filter files on the Teensy's SD card
   * @returns {Promise<Array<string>>} Array of filenames (empty when none)
   */
  async getFirFiles() {
    return this.request('GET', '/fir/files');
  }

  async updateFIREnabled(presetName, value) {
    return this.request('PUT', `/preset/fir/enabled?preset_name=${encodeURIComponent(presetName)}&state=${value ? 'on' : 'off'}`);
  }

  /**
   * Get specific preset data
   * @param {string} name - Preset name
   * @returns {Promise<Object>} Preset configuration
   */
  async getPreset(name) {
    return this.request('GET', `/preset?name=${encodeURIComponent(name)}`);
  }

  /**
   * Create or update the preference EQ set
   * @param {string} presetName - Name of the preset
   * @param {Array} peqPoints - Array of PEQ points
   */
  async savePrefEqSet(presetName, peqPoints) {
    return this.request('PUT', `/preset/eq?preset_name=${encodeURIComponent(presetName)}`, peqPoints);
  }

  async updateEqPoint(presetName, point) {
    return this.request('PUT', `/preset/eq/point?preset_name=${encodeURIComponent(presetName)}`, point);
  }

  /**
   * Replace a preset's dynamics (multiband compressor) block
   * @param {string} presetName - Name of the preset
   * @param {Object} dynamics - Full dynamics object
   */
  async savePresetDynamics(presetName, dynamics) {
    return this.request('PUT', `/preset/dynamics?preset_name=${encodeURIComponent(presetName)}`, dynamics);
  }

  /**
   * Audition one compressor band (transient, not stored). -1 restores all.
   * @param {number} band - Band index 0-2, or -1
   */
  async setCompSolo(band) {
    return this.request('POST', `/comp/solo?band=${band}`);
  }

  /**
   * Create new preset
   * @param {string} name - Preset name (must be unique)
   * @param {string} [template] - Template id (defaults to 2.1 server-side)
   */
  async createPreset(name, template = null) {
    const templateParam = template ? `&template=${encodeURIComponent(template)}` : '';
    return this.request('POST', `/preset?action=create&name=${encodeURIComponent(name)}${templateParam}`);
  }

  /**
   * List the available preset templates
   * @returns {Promise<Array>} [{id, label, description, outputsUsed}]
   */
  async getTemplates() {
    return this.request('GET', '/templates');
  }

  // ===== V1 CROSSOVER POINTS =====

  /**
   * Set a crossover point's frequency. Locked points require confirm=true
   * (the server rejects with 409 otherwise).
   * @param {string} presetName - Preset name
   * @param {string} id - Crossover point id (e.g. 'sub_xo')
   * @param {number} frequency - Frequency in Hz (within the point's min/max)
   * @param {boolean} [confirm] - Explicit confirmation for locked points
   */
  async setCrossoverPointFreq(presetName, id, frequency, confirm = false) {
    const confirmParam = confirm ? '&confirm=true' : '';
    return this.request('PUT',
      `/preset/crossover?preset_name=${encodeURIComponent(presetName)}&id=${encodeURIComponent(id)}&frequency=${frequency}${confirmParam}`);
  }

  /**
   * Enable/bypass a crossover point on every output filter referencing it.
   * Locked points require confirm=true; a bypass that would drop a
   * protected output's high-pass below its floor is rejected with 409.
   */
  async setCrossoverPointEnabled(presetName, id, enabled, confirm = false) {
    const confirmParam = confirm ? '&confirm=true' : '';
    return this.request('PUT',
      `/preset/crossover/enabled?preset_name=${encodeURIComponent(presetName)}&id=${encodeURIComponent(id)}&enabled=${enabled ? 'on' : 'off'}${confirmParam}`);
  }

  // ===== V1 OUTPUT CHANNELS =====

  _outputEndpoint(path, presetName, output, extraParams = '') {
    return `${path}?preset_name=${encodeURIComponent(presetName)}&output=${output}${extraParams}`;
  }

  async setOutputLabel(presetName, output, label) {
    return this.request('PUT', this._outputEndpoint('/preset/output/label', presetName, output,
      `&label=${encodeURIComponent(label)}`));
  }

  async setOutputEnabled(presetName, output, enabled) {
    return this.request('PUT', this._outputEndpoint('/preset/output/enabled', presetName, output,
      `&state=${enabled ? 'on' : 'off'}`));
  }

  /** Source mix: {left, right} gains 0..1 on the input buses */
  async setOutputSource(presetName, output, source) {
    return this.request('PUT', this._outputEndpoint('/preset/output/source', presetName, output), source);
  }

  /** Output gain in dB (-40..+10) */
  async setOutputGain(presetName, output, gainDb) {
    return this.request('PUT', this._outputEndpoint('/preset/output/gain', presetName, output,
      `&value=${gainDb}`));
  }

  async setOutputMute(presetName, output, mute) {
    return this.request('PUT', this._outputEndpoint('/preset/output/mute', presetName, output,
      `&state=${mute ? 'on' : 'off'}`));
  }

  async setOutputInvert(presetName, output, invert) {
    return this.request('PUT', this._outputEndpoint('/preset/output/invert', presetName, output,
      `&state=${invert ? 'on' : 'off'}`));
  }

  /** Output delay in microseconds (0..20000) */
  async setOutputDelay(presetName, output, delayUs) {
    return this.request('PUT', this._outputEndpoint('/preset/output/delay', presetName, output,
      `&value=${delayUs}`));
  }

  /**
   * Set an output's HP or LP section.
   * @param {string} which - 'hp' or 'lp'
   * @param {Object} filter - {mode:'off'} | {mode:'xover', xover:id} |
   *                          {mode:'manual', freq, type}
   */
  async setOutputFilter(presetName, output, which, filter) {
    return this.request('PUT', this._outputEndpoint('/preset/output/filter', presetName, output,
      `&which=${which}`), filter);
  }

  /** Replace an output's PEQ point array (max 10 points) */
  async saveOutputEq(presetName, output, points) {
    return this.request('PUT', this._outputEndpoint('/preset/output/eq', presetName, output), points);
  }

  /** Non-destructive per-output PEQ bypass (the stored points stay) */
  async setOutputEqEnabled(presetName, output, enabled) {
    return this.request('PUT', this._outputEndpoint('/preset/output/eq/enabled', presetName, output,
      `&state=${enabled ? 'on' : 'off'}`));
  }

  /** Update/append a single output PEQ point (hot path while dragging) */
  async updateOutputEqPoint(presetName, output, point) {
    return this.request('PUT', this._outputEndpoint('/preset/output/eq/point', presetName, output), point);
  }

  /** Assign a FIR file to an output ('' clears). 409 when the tap pool is exceeded. */
  async setOutputFir(presetName, output, file) {
    return this.request('PUT', this._outputEndpoint('/preset/output/fir', presetName, output,
      `&file=${encodeURIComponent(file)}`));
  }

  /** Tap pool status: {total, used, outputs:[{output, file, taps}]} */
  async getFirPool(presetName) {
    return this.request('GET', `/preset/fir/pool?preset_name=${encodeURIComponent(presetName)}`);
  }

  /**
   * Copy existing preset
   * @param {string} sourceName - Source preset name
   * @param {string} newName - New preset name
   */
  async copyPreset(sourceName, newName) {
    return this.request('POST', `/preset?action=copy&source=${encodeURIComponent(sourceName)}&destination=${encodeURIComponent(newName)}`);
  }

  /**
   * Rename preset
   * @param {string} oldName - Current preset name
   * @param {string} newName - New preset name
   */
  async renamePreset(oldName, newName) {
    return this.request('PUT', `/preset?action=rename&old_name=${encodeURIComponent(oldName)}&new_name=${encodeURIComponent(newName)}`);
  }

  /**
   * Delete preset
   * @param {string} name - Preset name
   */
  async deletePreset(name) {
    return this.request('DELETE', `/preset?name=${encodeURIComponent(name)}`);
  }

  // ===== SPEAKER DELAYS =====

  async setSpeakerDelayEnabled(presetName, enabled) {
    return this.request('PUT', `/preset/delay/enabled?preset_name=${encodeURIComponent(presetName)}&enabled=${enabled ? 'on' : 'off'}`);
  }

  // ===== EQ MANAGEMENT =====

  /**
   * Set EQ enabled state for a preset
   * @param {string} presetName - Preset name
   * @param {string} type - EQ type: "room" or "pref"
   * @param {boolean} enabled - Whether EQ is enabled
   */
  async setEQEnabled(presetName, type, enabled) {
    const validTypes = ['room', 'pref'];
    if (!validTypes.includes(type)) {
      throw new Error('EQ type must be "room" or "pref"');
    }
    if (typeof enabled !== 'boolean') {
      throw new Error('Enabled must be a boolean');
    }

    return this.request('PUT', `/preset/eq/enabled?preset_name=${encodeURIComponent(presetName)}&type=${type}&enabled=${enabled ? 'on' : 'off'}`);
  }

  /**
   * Set EQ configuration for a preset
   * @param {string} presetName - Preset name
   * @param {string} type - EQ type: "room" or "pref"
   * @param {number} spl - SPL value (0-120)
   * @param {Array} peqSet - Array of PEQ points with frequency, gain, and Q
   */

  // ===== CONFIGURATION =====

  /**
   * Backup configuration
   */
  async backup() {
    return this.request('GET', '/backup');
  }

  /**
   * Restore configuration
   */
  async restore(formData) {
    return this.request('POST', '/restore', formData, true);
  }

  // ===== VOLUME ===== //

  /**
   * Set the master volume. It is stored per preset, so omitting presetName
   * writes the active one (the live master volume); naming a preset sets the
   * level it will play at without changing what is playing now.
   * @param {number} volume - 0-100
   * @param {string} [presetName]
   */
  async setVolume(volume, presetName) {
    const preset = presetName ? `&preset_name=${encodeURIComponent(presetName)}` : '';
    return this.request('PUT', `/volume?value=${volume}${preset}`);
  }

  /**
   * Update input gains (0.0-1.0 linear). Accepts a partial object —
   * keys left out keep their current value on the device.
   * @param {Object} gains - e.g. { bluetooth: 0.8 } or { tone: 1 }
   */
  async updateInputGains(gains) {
    return this.request('PUT', `/gains/input`, gains);
  }

  // ===== SD RECORDER / PLAYER =====
  // Commands are asynchronous: the device replies "requested" immediately
  // and the resulting state arrives as recorderState websocket messages.

  /** Full recorder state + recordings list (served from the ESP's cache) */
  async getRecorder() {
    return this.request('GET', '/recorder');
  }

  /** Start recording the stereo input to a new auto-named file */
  async startRecording() {
    return this.request('POST', '/recorder/record/start');
  }

  async stopRecording() {
    return this.request('POST', '/recorder/record/stop');
  }

  /** Play a recording through the input chain */
  async playRecording(name) {
    return this.request('POST', `/recorder/play?name=${encodeURIComponent(name)}`);
  }

  async stopRecordingPlayback() {
    return this.request('POST', '/recorder/play/stop');
  }

  async deleteRecording(name) {
    return this.request('DELETE', `/recorder/file?name=${encodeURIComponent(name)}`);
  }

  // ===== WEBSOCKET LIVE UPDATES =====

  _setConnectionState(state) {
    if (this.connectionState === state) return;
    this.connectionState = state;
    this.statusListeners.forEach(cb => {
      try { cb(state); } catch (e) { console.error('Connection status listener failed:', e); }
    });
  }

  _openSocket() {
    const wsProtocol = window.location.protocol === 'https:' ? 'wss://' : 'ws://';
    // In dev the page is served by vite, not the device, so the live socket
    // goes to the mock server's standalone websocket port instead
    // (VITE_WS_URL overrides it, mirroring VITE_API_BASE_URL).
    const wsUrl = import.meta.env.DEV
      ? (import.meta.env.VITE_WS_URL || 'ws://localhost:8080')
      : `${wsProtocol}${window.location.host}/live-updates`;
    this.socket = new WebSocket(wsUrl);
    this._setConnectionState('connecting');

    this.socket.onopen = () => {
      this.reconnectDelay = 1000;
      this._setConnectionState('connected');
      if (this.pendingLiveMessage !== null) {
        this.socket.send(this.pendingLiveMessage);
        this.pendingLiveMessage = null;
      }
    };

    this.socket.onmessage = (event) => {
      let data;
      try {
        data = JSON.parse(event.data);
      } catch (error) {
        console.error('Failed to parse WebSocket message:', error);
        this.messageListeners.forEach(l => l.onError && l.onError(error));
        return;
      }
      this.messageListeners.forEach(l => l.onMessage(data));
    };

    this.socket.onerror = (error) => {
      console.error('WebSocket error:', error);
      this.messageListeners.forEach(l => l.onError && l.onError(error));
    };

    this.socket.onclose = (event) => {
      this.socket = null;
      this._setConnectionState('disconnected');
      this.messageListeners.forEach(l => l.onClose && l.onClose(event));
      this._scheduleReconnect();
    };
  }

  _scheduleReconnect() {
    if (this.reconnectTimer) return;
    this.reconnectTimer = setTimeout(() => {
      this.reconnectTimer = null;
      this.ensureLiveConnection();
    }, this.reconnectDelay);
    // Back off up to 30s so a powered-off device isn't hammered.
    this.reconnectDelay = Math.min(this.reconnectDelay * 2, 30000);
  }

  /**
   * Open the shared live-updates socket if it isn't already open.
   * Reconnects automatically whenever the connection drops.
   */
  ensureLiveConnection() {
    if (this.socket) return;
    this._openSocket();
  }

  /**
   * Subscribe to connection state changes ('disconnected' | 'connecting' |
   * 'connected'). The callback fires immediately with the current state.
   * @returns {Function} Unsubscribe function
   */
  onConnectionChange(callback) {
    this.statusListeners.add(callback);
    callback(this.connectionState);
    return () => this.statusListeners.delete(callback);
  }

  /**
   * Register a listener on the shared live-updates socket.
   * @param {Function} onMessage - Callback for incoming messages
   * @param {Function} onError - Callback for errors
   * @param {Function} onClose - Callback for connection close
   * @returns {Function} Unsubscribe function — call it on component unmount
   */
  connectLiveUpdates(onMessage, onError = null, onClose = null) {
    const listener = { onMessage, onError, onClose };
    this.messageListeners.add(listener);
    this.ensureLiveConnection();
    return () => this.messageListeners.delete(listener);
  }

  /**
   * Remove all message listeners. The socket itself stays open so the
   * connection indicator keeps working; listeners re-register on mount.
   */
  disconnectLiveUpdates() {
    this.messageListeners.clear();
  }

  /**
   * Send a raw text message over the live-updates socket. While the socket
   * is still connecting, the latest message is queued and flushed on open,
   * so the analyzer's first "rta:keepalive" isn't lost (that would delay
   * RTA streaming until the next keepalive tick).
   */
  sendLiveMessage(text) {
    if (this.isWebSocketConnected) {
      this.socket.send(text);
    } else {
      this.pendingLiveMessage = text;
    }
  }

  // ===== UTILITY METHODS =====

  /**
   * Check if the API is reachable
   * @returns {Promise<boolean>} True if API is reachable
   */
  async isOnline() {
    try {
      await this.getPresets();
      return true;
    } catch (error) {
      return false;
    }
  }

  /**
   * Get the current active preset
   * @returns {Promise<Object|null>} Current preset or null if none active
   */
  async getCurrentPreset() {
    const presets = await this.getPresets();
    const current = presets.find(preset => preset.isCurrent);
    if (current) {
      return await this.getPreset(current.name);
    }
    return null;
  }
}

// Shared singleton instance
export default new VybesAPI();

// Named export for class definition
export { VybesAPI };