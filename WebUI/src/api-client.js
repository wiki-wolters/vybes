// In production the API is same-origin (relative URLs), so the UI works
// identically over http://vybes.local and https://vybes.local - hardcoding
// a scheme would break one of them (mixed content under HTTPS).
const API_BASE_URL = import.meta.env.DEV ? 'http://vybes-mock.local' : ''

/*
 * Vybes DSP API Client - Enhanced Version
 * A comprehensive JavaScript client for interacting with the Vybes DSP system
 */
class VybesAPI {
  constructor() {
    this.baseUrl = API_BASE_URL;
    this.socket = null;

    // Live-update plumbing: one shared socket, many listeners, and
    // automatic reconnection with exponential backoff.
    this.messageListeners = new Set();
    this.statusListeners = new Set();
    this.connectionState = 'disconnected'; // 'disconnected' | 'connecting' | 'connected'
    this.reconnectTimer = null;
    this.reconnectDelay = 1000;
  }

  get isWebSocketConnected() {
    return this.socket && this.socket.readyState === WebSocket.OPEN;
  }

  /**
   * Make HTTP request with error handling
   * @param {string} method - HTTP method
   * @param {string} endpoint - API endpoint
   * @param {Object} body - Request body for POST requests
   * @returns {Promise<Object>} Response data
   */
  async request(method, endpoint, body = null, isFormData = false) {
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

    try {
      const response = await fetch(url, config);

      if (!response.ok) {
        throw new Error(`HTTP ${response.status}: ${response.statusText}`);
      }
      
      // Handle empty responses
      const text = await response.text();
      return text ? JSON.parse(text) : {};
    } catch (error) {
      console.error(`API request failed: ${method} ${endpoint}`, error);
      throw error;
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
   * @param {number} percent - Mute percentage (1-100)
   */
  async setMutePercent(percent) {
    if (percent < 1 || percent > 100) {
      throw new Error('Mute percent must be between 1 and 100');
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

  // ===== PRESET MANAGEMENT =====

  /**
   * Get all presets
   * @returns {Promise<Array>} Array of preset objects with name and isCurrent
   */
  async getPresets() {
    return this.request('GET', '/presets');
  }

  /**
   * Set speaker delay for a specific channel
   * @param {string} presetName - Name of the preset
   * @param {string} speaker - Speaker channel ('left', 'right', or 'sub')
   * @param {number} delayMs - Delay in milliseconds
   */
  async setSpeakerDelay(presetName, speaker, delayMs) {
    return this.request('PUT', `/preset/delay?preset_name=${encodeURIComponent(presetName)}&speaker=${speaker}&value=${delayMs}`);
  }

  /**
   * Set crossover frequency for a preset
   * @param {string} presetName - Name of the preset
   * @param {number} frequency - Crossover frequency in Hz
   */
  async setCrossoverFreq(presetName, frequency) {
    return this.request('PUT', `/preset/crossover?preset_name=${encodeURIComponent(presetName)}&frequency=${frequency}`);
  }

  async updateFIREnabled(presetName, value) {
    return this.request('PUT', `/preset/fir/enabled?preset_name=${encodeURIComponent(presetName)}&state=${value ? 'on' : 'off'}`);
  }

  /**
   * Set FIR filter for a channel
   * @param {string} presetName - Name of the preset
   * @param {string} channel - Channel ('left', 'right', or 'sub')
   * @param {string} filterName - Name of the FIR filter file (empty string to clear)
   */
  async setFirFilter(presetName, channel, filterName) {
    return this.request('PUT', `/preset/fir?preset_name=${encodeURIComponent(presetName)}&speaker=${channel}&file=${encodeURIComponent(filterName)}`);
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
   * Create new preset
   * @param {string} name - Preset name (must be unique)
   */
  async createPreset(name) {
    return this.request('POST', `/preset?action=create&name=${encodeURIComponent(name)}`);
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

  /**
   * Set speaker delay
   * @param {string} presetName - Preset name
   * @param {string} speaker - Speaker type: "left", "right", or "sub"
   * @param {number} delayUs - Delay in microseconds (float)
   */
  async setSpeakerDelay(presetName, speaker, delayUs) {
    const validSpeakers = ['left', 'right', 'sub'];
    if (!validSpeakers.includes(speaker)) {
      throw new Error('Speaker must be "left", "right", or "sub"');
    }
    return this.request('PUT', `/preset/delay?preset_name=${encodeURIComponent(presetName)}&speaker=${speaker}&value=${delayUs}`);
  }

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

  // ===== CROSSOVER =====

  /**
   * Set crossover enabled state for a preset
   * @param {string} presetName - Preset name
   * @param {boolean} enabled - Whether crossover is enabled
   */
  async updateCrossoverEnabled(presetName, enabled) {
    return this.request('PUT', `/preset/crossover/enabled?preset_name=${encodeURIComponent(presetName)}&enabled=${enabled ? 'on' : 'off'}`);
  }

  /**
   * Set crossover configuration
   * @param {string} presetName - Preset name
   * @param {number} frequency - Crossover frequency (40-500 Hz)
   */
  async updateCrossoverFreq(presetName, frequency) {
    if (frequency < 40 || frequency > 500) {
      throw new Error('Crossover frequency must be between 40 and 500 Hz');
    }
    return this.request('PUT', `/preset/crossover?preset_name=${encodeURIComponent(presetName)}&frequency=${frequency}`);
  }

  // ===== EQUAL LOUDNESS =====

  /**
   * Set equal loudness compensation
   * @param {string} presetName - Preset name
   * @param {boolean} state - true for on, false for off
   */
  async setEqualLoudness(presetName, state) {
    const stateStr = state ? 'on' : 'off';
    return this.request('PUT', `/preset/equal-loudness?preset_name=${encodeURIComponent(presetName)}&state=${stateStr}`);
  }

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

  // ===== SPEAKER GAIN ===== //
  async setGlobalSpeakerGain(speaker, gain) {
    return this.request('PUT', `/gains/speaker?speaker=${speaker}&value=${gain}`);
  }

  async getPresetGains(presetName) {
    return this.request('GET', `/preset/gains?preset_name=${encodeURIComponent(presetName)}`);
  }

  async setPresetGains(presetName, gains) {
    return this.request('PUT', `/preset/gains?preset_name=${encodeURIComponent(presetName)}`, gains);
  }

  // ===== VOLUME ===== //
  async setVolume(volume) {
    return this.request('PUT', `/volume?value=${volume}`);
  }

  /**
   * Set input gains
   * @param {number} bluetooth - Bluetooth gain (0.0-1.0)
   * @param {number} spdif - SPDIF gain (0.0-1.0)
   * @param {number} tone - Tone generator gain (0.0-1.0)
   */
  async setInputGains(bluetooth, spdif, usb, tone) {
    return this.request('PUT', `/gains/input`, { bluetooth, spdif, usb, tone });
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
    // goes to the mock server's standalone websocket port instead.
    const wsUrl = import.meta.env.DEV
      ? 'ws://localhost:8080'
      : `${wsProtocol}${window.location.host}/live-updates`;
    this.socket = new WebSocket(wsUrl);
    this._setConnectionState('connecting');

    this.socket.onopen = () => {
      this.reconnectDelay = 1000;
      this._setConnectionState('connected');
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
   * Send a raw text message over the live-updates socket. No-op when the
   * socket isn't open. Used by the analyzer's "rta:keepalive".
   */
  sendLiveMessage(text) {
    if (this.isWebSocketConnected) {
      this.socket.send(text);
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

// Export for different module systems
if (typeof module !== 'undefined' && module.exports) {
  module.exports = new VybesAPI();
  module.exports.default = VybesAPI;
} else if (typeof window !== 'undefined') {
  window.VybesAPI = new VybesAPI();
}

// ES6 default export
export default new VybesAPI();

// Named export for class definition
export { VybesAPI };