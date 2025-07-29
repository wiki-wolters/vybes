const API_BASE_URL = import.meta.env.VITE_API_BASE_URL || 'http://vybes.local'

/*
 * Vybes DSP API Client - Enhanced Version
 * A comprehensive JavaScript client for interacting with the Vybes DSP system
 */
class VybesAPI {
  constructor(baseUrl = API_BASE_URL) {
    this.baseUrl = baseUrl;
    this.socket = null;
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
    const url = `${this.baseUrl}${endpoint}`;
    const config = {
      method: method.toUpperCase(),
      headers: {
        'Content-Type': 'application/json',
      },
    };

    if (body && (method === 'POST' || method === 'PUT')) {
      if (isFormData) {
        delete config.headers['Content-Type'];
        config.body = body;
      } else {
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

  // ===== CALIBRATION =====
  
  /**
   * Set calibration SPL value
   * @param {number} spl - SPL value (40-120)
   */
  async calibrate(spl) {
    if (spl < 40 || spl > 120) {
      throw new Error('SPL must be between 40 and 120');
    }
    return this.request('PUT', `/calibrate/${spl}`);
  }

  /**
   * Get calibration status
   * @returns {Promise<Object>} Calibration data including SPL value and status
   */
  async getCalibration() {
    return this.request('GET', '/calibration');
  }

  // ===== SYSTEM CONTROLS =====

  /**
   * Turn subwoofer on/off
   * @param {boolean} state - true for on, false for off
   */
  async setSubwoofer(state) {
    const stateStr = state ? 'on' : 'off';
    return this.request('PUT', `/sub/${stateStr}`);
  }

  /**
   * Bypass DSP on/off
   * @param {boolean} state - true for on, false for off
   */
  async setBypass(state) {
    const stateStr = state ? 'on' : 'off';
    return this.request('PUT', `/bypass/${stateStr}`);
  }

  /**
   * Mute on/off
   * @param {boolean} state - true for on, false for off
   */
  async setMute(state) {
    const stateStr = state ? 'on' : 'off';
    return this.request('PUT', `/mute/${stateStr}`);
  }

  /**
   * Set mute percentage
   * @param {number} percent - Mute percentage (1-100)
   */
  async setMutePercent(percent) {
    if (percent < 1 || percent > 100) {
      throw new Error('Mute percent must be between 1 and 100');
    }
    return this.request('PUT', `/mute/percent/${percent}`);
  }

  /**
   * Set active preset
   * @param {string} name - Preset name to activate
   */
  async setActivePreset(name) {
    return this.request('PUT', `/preset/active/${encodeURIComponent(name)}`);
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
   * @param {number} frequency - Frequency (10-20000 Hz)
   * @param {number} volume - Volume (1-100)
   */
  async generateTone(frequency, volume) {
    if (frequency < 10 || frequency > 20000) {
      throw new Error('Frequency must be between 10 and 20000 Hz');
    }
    if (volume < 1 || volume > 100) {
      throw new Error('Volume must be between 1 and 100');
    }
    return this.request('PUT', `/generate/tone/${frequency}/${volume}`);
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
    return this.request('PUT', `/generate/noise/${volume}`);
  }

  /**
   * Play test pulse (100hz for 200ms on each output)
   */
  async playPulse() {
    return this.request('PUT', '/pulse');
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
   * Get speaker delays for a preset
   * @param {string} presetName - Name of the preset
   * @returns {Promise<Object>} Object containing delays for left, right, and sub
   */
  async getSpeakerDelays(presetName) {
    return this.request('GET', `/preset/${encodeURIComponent(presetName)}/delays`);
  }

  /**
   * Set speaker delay for a specific channel
   * @param {string} presetName - Name of the preset
   * @param {string} speaker - Speaker channel ('left', 'right', or 'sub')
   * @param {number} delayMs - Delay in milliseconds
   */
  async setSpeakerDelay(presetName, speaker, delayMs) {
    return this.request('PUT', `/preset/${encodeURIComponent(presetName)}/delay/${speaker}/${delayMs}`);
  }

  /**
   * Get crossover settings for a preset
   * @param {string} presetName - Name of the preset
   * @returns {Promise<Object>} Crossover settings
   */
  async getCrossover(presetName) {
    return this.request('GET', `/preset/${encodeURIComponent(presetName)}/crossover`);
  }

  /**
   * Set crossover frequency for a preset
   * @param {string} presetName - Name of the preset
   * @param {number} frequency - Crossover frequency in Hz
   */
  async setCrossoverFreq(presetName, frequency) {
    return this.request('PUT', `/preset/${encodeURIComponent(presetName)}/crossover/freq/${frequency}`);
  }

  async updateFIREnabled(presetName, value) {
    return this.request('PUT', `/preset/${encodeURIComponent(presetName)}/fir/enabled/${value ? 'on' : 'off'}`);
  }

  /**
   * Set FIR filter for a channel
   * @param {string} presetName - Name of the preset
   * @param {string} channel - Channel ('left', 'right', or 'sub')
   * @param {string} filterName - Name of the FIR filter file (empty string to clear)
   */
  async setFirFilter(presetName, channel, filterName) {
    return this.request('PUT', `/preset/${encodeURIComponent(presetName)}/fir/file/${channel}/${encodeURIComponent(filterName)}`);
  }

  /**
   * Get all EQ sets for a preset and type
   * @param {string} presetName - Name of the preset
   * @param {string} type - EQ type ('room' or 'pref')
   * @returns {Promise<Array>} Array of EQ sets with SPL and PEQ data
   */
  async getEqSets(presetName, type) {
    return this.request('GET', `/preset/${encodeURIComponent(presetName)}/eq/${type}`);
  }

  /**
   * Get specific preset data
   * @param {string} name - Preset name
   * @returns {Promise<Object>} Preset configuration
   */
  async getPreset(name) {
    return this.request('GET', `/preset/${encodeURIComponent(name)}`);
  }

  /**
   * Create or update the preference EQ set
   * @param {string} presetName - Name of the preset
   * @param {Array} peqPoints - Array of PEQ points
   */
  async savePrefEqSet(presetName, peqPoints) {
    return this.request('PUT', `/preset/${encodeURIComponent(presetName)}/eq/pref/0`, peqPoints);
  }

  /**
   * Create new preset
   * @param {string} name - Preset name (must be unique)
   */
  async createPreset(name) {
    return this.request('POST', `/preset/create/${encodeURIComponent(name)}`);
  }

  /**
   * Copy existing preset
   * @param {string} sourceName - Source preset name
   * @param {string} newName - New preset name
   */
  async copyPreset(sourceName, newName) {
    return this.request('POST', `/preset/copy/${encodeURIComponent(sourceName)}/${encodeURIComponent(newName)}`);
  }

  /**
   * Rename preset
   * @param {string} oldName - Current preset name
   * @param {string} newName - New preset name
   */
  async renamePreset(oldName, newName) {
    return this.request('PUT', `/preset/rename/${encodeURIComponent(oldName)}/${encodeURIComponent(newName)}`);
  }

  /**
   * Delete preset
   * @param {string} name - Preset name
   */
  async deletePreset(name) {
    return this.request('DELETE', `/preset/${encodeURIComponent(name)}`);
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
    return this.request('PUT', `/preset/${encodeURIComponent(presetName)}/delay/${speaker}/${delayUs}`);
  }

  async setSpeakerDelayEnabled(presetName, enabled) {
    return this.request('PUT', `/preset/${encodeURIComponent(presetName)}/delay/enabled/${enabled ? 'on' : 'off'}`);
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

    return this.request('PUT', `/preset/${encodeURIComponent(presetName)}/eq/${type}/enabled/${enabled ? 'on' : 'off'}`);
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
    return this.request('PUT', `/preset/${encodeURIComponent(presetName)}/crossover/enabled/${enabled ? 'on' : 'off'}`);
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
    return this.request('PUT', `/preset/${encodeURIComponent(presetName)}/crossover/freq/${frequency}`);
  }

  // ===== EQUAL LOUDNESS =====

  /**
   * Set equal loudness compensation
   * @param {string} presetName - Preset name
   * @param {boolean} state - true for on, false for off
   */
  async setEqualLoudness(presetName, state) {
    const stateStr = state ? 'on' : 'off';
    return this.request('PUT', `/preset/${encodeURIComponent(presetName)}/equal-loudness/${stateStr}`);
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
    return this.request('PUT', `/speaker/${speaker}/gain/${gain}`);
  }

  /**
   * Set input gains
   * @param {number} bluetooth - Bluetooth gain (0.0-1.0)
   * @param {number} spdif - SPDIF gain (0.0-1.0)
   * @param {number} tone - Tone generator gain (0.0-1.0)
   */
  async setInputGains(bluetooth, spdif, tone) {
    return this.request('PUT', `/gains/input`, { bluetooth, spdif, tone });
  }

  // ===== WEBSOCKET LIVE UPDATES =====

  /**
   * Connect to live updates WebSocket
   * @param {Function} onMessage - Callback for incoming messages
   * @param {Function} onError - Callback for errors
   * @param {Function} onClose - Callback for connection close
   */
  connectLiveUpdates(onMessage, onError = null, onClose = null) {
    if (this.socket) {
      this.socket.close();
    }

    const wsUrl = this.baseUrl.replace('http://', 'ws://').replace('https://', 'wss://');
    const url = new URL(wsUrl);
    
    this.socket = new WebSocket(`${url.toString().replace(/\/$/, '')}/live-updates`);

    this.socket.onmessage = (event) => {
      try {
        const data = JSON.parse(event.data);
        onMessage(data);
      } catch (error) {
        console.error('Failed to parse WebSocket message:', error);
        if (onError) onError(error);
      }
    };

    this.socket.onerror = (error) => {
      console.error('WebSocket error:', error);
      if (onError) onError(error);
    };

    this.socket.onclose = (event) => {
      console.log('WebSocket connection closed:', event);
      if (onClose) onClose(event);
    };

    this.socket.onopen = () => {
      console.log('WebSocket connection established');
    };
  }

  /**
   * Disconnect from live updates WebSocket
   */
  disconnectLiveUpdates() {
    if (this.socket) {
      this.socket.close();
      this.socket = null;
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