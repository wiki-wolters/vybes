const express = require('express');
const sqlite3 = require('sqlite3').verbose();
const WebSocket = require('ws');
const cors = require('cors');
const path = require('path');

const app = express();
const expressStaticGzip = require('express-static-gzip');
const PORT = 80; // Standard HTTP port for vybes.local

// Middleware
app.use(cors());
app.use(express.json());

if (process.env.NODE_ENV === 'production') {
  // Production: serve built files
  app.use('/', expressStaticGzip('../WebUI/dist'));
} else {
  // Development: just serve a simple API status page or redirect
  app.get('/', (req, res) => {
    res.json({ 
      message: 'Vybes API Server - Development Mode', 
      frontend: 'Run yarn dev in WebUI directory',
      api: 'http://localhost:80'
    });
  });
}

// Initialize SQLite database
const dbPath = path.join(__dirname, 'vybes.db');
const db = new sqlite3.Database(dbPath);

// Initialize database tables
db.serialize(() => {
  // System settings table
  db.run(`CREATE TABLE IF NOT EXISTS system_settings (
    key TEXT PRIMARY KEY,
    value TEXT
  )`);

  // Presets table
  db.run(`CREATE TABLE IF NOT EXISTS presets (
    name TEXT PRIMARY KEY,
    is_current INTEGER DEFAULT 0,
    is_speaker_delay_enabled INTEGER DEFAULT 0,
    speaker_delays TEXT DEFAULT '{"left": 0, "right": 0, "sub": 0}',
    is_crossover_enabled INTEGER DEFAULT 0,
    crossover_freq TEXT DEFAULT '80',
    is_fir_enabled INTEGER DEFAULT 0,
    fir_left TEXT DEFAULT '',
    fir_right TEXT DEFAULT '',
    fir_sub TEXT DEFAULT '',
    gains TEXT DEFAULT '{"left": 1.0, "right": 1.0, "sub": 1.0}',
    is_preference_eq_enabled INTEGER DEFAULT 0,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP
  )`);

  // EQ configurations table
  db.run(`CREATE TABLE IF NOT EXISTS eq_configs (
    preset_name TEXT,
    type TEXT,
    spl INTEGER,
    peq_data TEXT,
    PRIMARY KEY (preset_name, type, spl),
    FOREIGN KEY (preset_name) REFERENCES presets (name) ON DELETE CASCADE
  )`);

  // Insert default settings if they don't exist
  db.get("SELECT value FROM system_settings WHERE key = 'sub_gain'", (err, row) => {
    if (!row) {
      const defaultSettings = [
        ['sub_gain', '1.0'],
        ['left_gain', '1.0'],
        ['right_gain', '1.0'],
        ['mute_state', 'off'],
        ['mute_percent', '0'],
        ['tone_frequency', '1000'],
        ['tone_volume', '50'],
        ['noise_volume', '0'],
        ['bluetooth_gain', '1.0'],
        ['spdif_gain', '0.0'],
        ['usb_gain', '1.0'],
        ['tone_gain', '0.0']
      ];
      
      const stmt = db.prepare("INSERT OR IGNORE INTO system_settings (key, value) VALUES (?, ?)");
      defaultSettings.forEach(([key, value]) => {
        stmt.run(key, value);
      });
      stmt.finalize();
    }
  });

  // Create default preset if none exist
  db.get("SELECT COUNT(*) as count FROM presets", (err, row) => {
    if (row.count === 0) {
      db.run(`INSERT INTO presets (name, is_current) VALUES ('Default', 1)`);
    }
  });
});

// WebSocket server for live updates
const wss = new WebSocket.Server({ port: 8080 });

// Broadcast to all connected WebSocket clients
function broadcast(data) {
  const message = JSON.stringify(data);
  wss.clients.forEach(client => {
    if (client.readyState === WebSocket.OPEN) {
      client.send(message);
    }
  });
}

// WebSocket connection handler
wss.on('connection', (ws) => {
  console.log('WebSocket client connected');
  
  ws.on('close', () => {
    console.log('WebSocket client disconnected');
  });
});

// Helper functions
function getSetting(key) {
  return new Promise((resolve, reject) => {
    db.get("SELECT value FROM system_settings WHERE key = ?", [key], (err, row) => {
      if (err) reject(err);
      else resolve(row ? row.value : null);
    });
  });
}

function setSetting(key, value) {
  return new Promise((resolve, reject) => {
    db.run("INSERT OR REPLACE INTO system_settings (key, value) VALUES (?, ?)", [key, value], (err) => {
      if (err) reject(err);
      else resolve();
    });
  });
}

// ===== API ROUTES - MATCHING ESP8266 ROUTES =====

// System Status
app.get('/status', async (req, res) => {
  try {
    const [
      subGain,
      leftGain,
      rightGain,
      muteState,
      mutePercent,
      toneFrequency,
      toneVolume,
      noiseVolume,
      bluetoothGain,
      spdifGain,
      usbGain,
      toneGain
    ] = await Promise.all([
      getSetting('sub_gain'),
      getSetting('left_gain'),
      getSetting('right_gain'),
      getSetting('mute_state'),
      getSetting('mute_percent'),
      getSetting('tone_frequency'),
      getSetting('tone_volume'),
      getSetting('noise_volume'),
      getSetting('bluetooth_gain'),
      getSetting('spdif_gain'),
      getSetting('usb_gain'),
      getSetting('tone_gain')
    ]);

    // Get current preset
    const currentPreset = await new Promise((resolve, reject) => {
      db.get("SELECT name FROM presets WHERE is_current = 1", (err, row) => {
        if (err) reject(err);
        else resolve(row ? row.name : null);
      });
    });

    res.json({
      speakerGains: {
        sub: subGain ? parseFloat(subGain) : 0,
        left: leftGain ? parseFloat(leftGain) : 0,
        right: rightGain ? parseFloat(rightGain) : 0
      },
      mute: {
        state: muteState || 'off',
        percent: mutePercent ? parseInt(mutePercent) : 0
      },
      tone: {
        frequency: toneFrequency ? parseInt(toneFrequency) : 1000,
        volume: toneVolume ? parseInt(toneVolume) : 50
      },
      noise: {
        volume: noiseVolume ? parseInt(noiseVolume) : 0
      },
      inputGains: {
        bluetooth: bluetoothGain ? parseFloat(bluetoothGain) : 0,
        spdif: spdifGain ? parseFloat(spdifGain) : 0,
        usb: usbGain ? parseFloat(usbGain) : 0,
        tone: toneGain ? parseFloat(toneGain) : 0
      },
      volume: volume ? parseInt(volume) : 50,
      currentPreset
    });
  } catch (error) {
    res.status(500).json({ error: error.message });
  }
});

app.put('/volume', async (req, res) => {
  const { value } = req.query;
  const volume = parseInt(value);
  if (isNaN(volume) || volume < 0 || volume > 100) {
    return res.status(400).json({ error: 'Volume must be between 0 and 100' });
  }

  try {
    await setSetting('volume', volume.toString());
    broadcast({ messageType: 'volumeChanged', volume });
    res.json({ success: true, volume });
  } catch (error) {
    res.status(500).json({ error: error.message });
  }
});

// Mute controls
app.put('/mute/percent', async (req, res) => {
  const percent = req.query.percent || req.body.percent;
  const percentNum = parseInt(percent);
  
  if (isNaN(percentNum) || percentNum < 1 || percentNum > 100) {
    return res.status(400).json({ error: 'Percent must be between 1 and 100' });
  }
  
  try {
    await setSetting('mute_percent', percent.toString());
    broadcast({ event: 'mute_percent', percent: percentNum });
    res.json({ success: true, percent: percentNum });
  } catch (error) {
    res.status(500).json({ error: error.message });
  }
});

app.put('/mute', async (req, res) => {
  const state = req.query.state || req.body.state;
  
  if (!['on', 'off'].includes(state)) {
    return res.status(400).json({ error: 'State must be "on" or "off"' });
  }
  
  try {
    await setSetting('mute_state', state);
    broadcast({ event: 'mute', state });
    res.json({ success: true, state });
  } catch (error) {
    res.status(500).json({ error: error.message });
  }
});

// Speaker & Input gains
app.put('/gains/speaker', async (req, res) => {
  const { speaker, gain } = req.query.speaker && req.query.gain 
    ? req.query 
    : req.body;
    
  const gainNum = parseFloat(gain);
  
  if (gainNum < 0 || gainNum > 2) {
    return res.status(400).json({ error: 'Gain must be between 0 and 2' });
  }
  
  if (!['left', 'right', 'sub'].includes(speaker)) {
    return res.status(400).json({ error: 'Speaker must be "left", "right", or "sub"' });
  }
  
  try {
    await setSetting(`${speaker}_gain`, gain.toString());
    broadcast({ event: `${speaker}_gain`, gain: gainNum });
    res.json({ success: true, gain: gainNum });
  } catch (error) {
    res.status(500).json({ error: error.message });
  }
});

app.put('/gains/input', async (req, res) => {
  const { bluetooth, spdif, usb, tone } = req.body;

  if (bluetooth === undefined || spdif === undefined || usb === undefined || tone === undefined) {
    return res.status(400).json({ error: 'Missing gain values' });
  }

  try {
    await setSetting('bluetooth_gain', bluetooth.toString());
    await setSetting('spdif_gain', spdif.toString());
    await setSetting('usb_gain', usb.toString());
    await setSetting('tone_gain', tone.toString());

    broadcast({ event: 'input_gains', gains: { bluetooth, spdif, usb, tone } });
    res.json({ success: true, gains: { bluetooth, spdif, usb, tone } });
  } catch (error) {
    res.status(500).json({ error: error.message });
  }
});

app.get('/preset/gains', (req, res) => {
  const presetName = req.query.preset_name;
  if (!presetName) {
    return res.status(400).json({ error: 'Preset name is required' });
  }

  db.get("SELECT gains FROM presets WHERE name = ?", [presetName], (err, row) => {
    if (err) {
      return res.status(500).json({ error: err.message });
    }
    if (!row) {
      return res.status(404).json({ error: 'Preset not found' });
    }
    res.json(JSON.parse(row.gains));
  });
});

app.put('/preset/gains', (req, res) => {
  const presetName = req.query.preset_name;
  const { left, right, sub } = req.body;

  if (!presetName) {
    return res.status(400).json({ error: 'Preset name is required' });
  }

  if (left === undefined || right === undefined || sub === undefined) {
    return res.status(400).json({ error: 'Missing gain values' });
  }

  const gains = JSON.stringify({ left, right, sub });

  db.run("UPDATE presets SET gains = ? WHERE name = ?", [gains, presetName], function(err) {
    if (err) {
      return res.status(500).json({ error: err.message });
    }
    if (this.changes === 0) {
      return res.status(404).json({ error: 'Preset not found' });
    }
    broadcast({ event: 'preset_gains_updated', preset: presetName, gains: req.body });
    res.json({ success: true });
  });
});

// FIR Filter Management
app.get('/fir/files', (req, res) => {
  // Return a list of sample FIR filter files
  res.json([
    'fir_flat.txt',
    'fir_room1.txt',
    'fir_room2.txt',
    'fir_speaker1.txt',
    'fir_speaker2.txt'
  ]);
});

app.put('/preset/fir/enabled', (req, res) => {
  const presetName = req.query.name || req.body.name;
  const state = req.query.state || req.body.state;
  const enabled = state === 'on' ? 1 : 0;
  
  if (!presetName) {
    return res.status(400).json({ error: 'Preset name is required' });
  }
  
  db.run("UPDATE presets SET is_fir_enabled = ? WHERE name = ?", [enabled, presetName], function(err) {
    if (err) {
      return res.status(500).json({ error: err.message });
    }
    
    if (this.changes === 0) {
      return res.status(404).json({ error: 'Preset not found' });
    }
    
    broadcast({ event: 'fir_enabled', preset: presetName, enabled: Boolean(enabled) });
    res.json({ success: true, preset: presetName, enabled: Boolean(enabled) });
  });
});

app.put('/preset/fir', (req, res) => {
  const { name: presetName, channel, filter } = req.query.name && req.query.channel && req.query.filter 
    ? req.query 
    : req.body;
  
  if (!presetName || !channel || filter === undefined) {
    return res.status(400).json({ error: 'Preset name, channel, and filter are required' });
  }
  
  if (!['left', 'right', 'sub'].includes(channel)) {
    return res.status(400).json({ error: 'Invalid channel. Must be left, right, or sub' });
  }
  
  const column = `fir_${channel}`;
  db.run(`UPDATE presets SET ${column} = ? WHERE name = ?`,
    [filter, presetName],
    function(err) {
      if (err) {
        return res.status(500).json({ error: err.message });
      }
      
      if (this.changes === 0) {
        return res.status(404).json({ error: 'Preset not found' });
      }
      
      broadcast({
        event: 'fir_updated',
        preset: presetName,
        channel,
        filter
      });
      
      res.json({ success: true, channel, filter });
    }
  );
});

// Active preset
app.put('/preset/active', (req, res) => {
  const name = req.query.name || req.body.name;
  
  if (!name) {
    return res.status(400).json({ error: 'Preset name is required' });
  }
  
  // First, unset all current presets
  db.run("UPDATE presets SET is_current = 0", (err) => {
    if (err) {
      return res.status(500).json({ error: err.message });
    }
    
    // Then set the new current preset
    db.run("UPDATE presets SET is_current = 1 WHERE name = ?", [name], function(err) {
      if (err) {
        return res.status(500).json({ error: err.message });
      }
      
      if (this.changes === 0) {
        return res.status(404).json({ error: 'Preset not found' });
      }
      
      broadcast({ event: 'preset', action: 'activated', name });
      res.json({ success: true, activePreset: name });
    });
  });
});

// Feature enablement
app.put('/preset/delay/enabled', (req, res) => {
  const presetName = req.query.name || req.body.name;
  const state = req.query.state || req.body.state;
  
  if (!presetName) {
    return res.status(400).json({ error: 'Preset name is required' });
  }
  
  if (!['on', 'off'].includes(state)) {
    return res.status(400).json({ error: 'State must be "on" or "off"' });
  }
  
  const delayEnabled = state === 'on' ? 1 : 0;
  
  db.run("UPDATE presets SET is_speaker_delay_enabled = ? WHERE name = ?", [delayEnabled, presetName], function(err) {
    if (err) {
      return res.status(500).json({ error: err.message });
    }
    
    if (this.changes === 0) {
      return res.status(404).json({ error: 'Preset not found' });
    }
    
    broadcast({ event: 'is_speaker_delay_enabled', name: presetName, state });
    res.json({ success: true, name: presetName, state });
  });
});

app.put('/preset/eq/enabled', (req, res) => {
  const presetName = req.query.name || req.body.name;
  const state = req.query.state || req.body.state;
  
  if (!presetName) {
    return res.status(400).json({ error: 'Preset name is required' });
  }
  
  if (!['on', 'off'].includes(state)) {
    return res.status(400).json({ error: 'State must be "on" or "off"' });
  }
  
  const eqEnabled = state === 'on' ? 1 : 0;
  
  db.run("UPDATE presets SET is_preference_eq_enabled = ? WHERE name = ?", [eqEnabled, presetName], function(err) {
    if (err) {
      return res.status(500).json({ error: err.message });
    }
    
    if (this.changes === 0) {
      return res.status(404).json({ error: 'Preset not found' });
    }
    
    broadcast({ event: 'is_preference_eq_enabled', preset: presetName, state });
    res.json({ success: true, preset: presetName, state });
  });
});

app.put('/preset/crossover/enabled', (req, res) => {
  const presetName = req.query.name || req.body.name;
  const state = req.query.state || req.body.state;
  
  if (!presetName) {
    return res.status(400).json({ error: 'Preset name is required' });
  }
  
  if (!['on', 'off'].includes(state)) {
    return res.status(400).json({ error: 'State must be "on" or "off"' });
  }
  
  const crossoverEnabled = state === 'on' ? 1 : 0;
  
  db.run("UPDATE presets SET is_crossover_enabled = ? WHERE name = ?", [crossoverEnabled, presetName], function(err) {
    if (err) {
      return res.status(500).json({ error: err.message });
    }
    
    if (this.changes === 0) {
      return res.status(404).json({ error: 'Preset not found' });
    }
    
    broadcast({ event: 'crossover', preset: presetName, state });
    res.json({ success: true, preset: presetName });
  });
});

// Speaker Configuration - Delay
app.put('/preset/delay', (req, res) => {
  const { name: presetName, speaker, delay } = req.query.name && req.query.speaker && req.query.delay 
    ? req.query 
    : req.body;
  
  if (!presetName || !speaker || delay === undefined) {
    return res.status(400).json({ error: 'Preset name, speaker, and delay are required' });
  }
  
  const delayUs = parseFloat(delay);
  
  if (!['left', 'right', 'sub'].includes(speaker)) {
    return res.status(400).json({ error: 'Speaker must be "left", "right", or "sub"' });
  }
  
  db.get("SELECT speaker_delays FROM presets WHERE name = ?", [presetName], (err, preset) => {
    if (err) {
      return res.status(500).json({ error: err.message });
    }
    if (!preset) {
      return res.status(404).json({ error: 'Preset not found' });
    }
    
    const delays = JSON.parse(preset.speaker_delays);
    delays[speaker] = delayUs;
    
    db.run("UPDATE presets SET speaker_delays = ? WHERE name = ?", [JSON.stringify(delays), presetName], (err) => {
      if (err) {
        return res.status(500).json({ error: err.message });
      }
      
      broadcast({ event: 'speaker_delay', speaker, delayUs, preset: presetName });
      res.json({ success: true, speaker, delayUs, preset: presetName });
    });
  });
});

// EQ Points (JSON handler)
app.put('/preset/eq', (req, res) => {
  const presetName = req.query.name || req.body.name || 'Default'; // Default if not specified
  const type = 'pref';
  const spl = 0;
  const peqPoints = req.body.peqPoints || req.body;

  if (!Array.isArray(peqPoints)) {
    return res.status(400).json({ error: 'peqPoints must be an array' });
  }

  // First, check if the preset exists
  db.get("SELECT name FROM presets WHERE name = ?", [presetName], (err, preset) => {
    if (err) {
      return res.status(500).json({ error: err.message });
    }

    if (!preset) {
      return res.status(404).json({ error: 'Preset not found' });
    }

    // Check if EQ set already exists for this SPL and type
    db.get(
      "SELECT * FROM eq_configs WHERE preset_name = ? AND type = ? AND spl = ?",
      [presetName, type, spl],
      (err, existing) => {
        if (err) {
          return res.status(500).json({ error: err.message });
        }

        const peqData = JSON.stringify(peqPoints);

        if (existing) {
          // Update existing EQ set
          db.run(
            "UPDATE eq_configs SET peq_data = ? WHERE preset_name = ? AND type = ? AND spl = ?",
            [peqData, presetName, type, spl],
            function(err) {
              if (err) {
                return res.status(500).json({ error: err.message });
              }

              broadcast({
                event: 'eq_updated',
                preset: presetName,
                type,
                spl,
                peqPoints
              });

              res.json({ success: true, spl, peqPoints });
            }
          );
        } else {
          // Create new EQ set
          db.run(
            "INSERT INTO eq_configs (preset_name, type, spl, peq_data) VALUES (?, ?, ?, ?)",
            [presetName, type, spl, peqData],
            function(err) {
              if (err) {
                return res.status(500).json({ error: err.message });
              }

              broadcast({
                event: 'eq_created',
                preset: presetName,
                type,
                spl,
                peqPoints
              });

              res.status(201).json({ success: true, spl, peqPoints });
            }
          );
        }
      }
    );
  });
});

// Crossover
app.put('/preset/crossover', (req, res) => {
  const { name: presetName, freq } = req.query.name && req.query.freq 
    ? req.query 
    : req.body;
  
  if (!presetName || !freq) {
    return res.status(400).json({ error: 'Preset name and frequency are required' });
  }
  
  const frequency = parseInt(freq);
  
  if (isNaN(frequency) || frequency < 20 || frequency > 500) {
    return res.status(400).json({ error: 'Frequency must be between 20 and 500 Hz' });
  }
  
  db.run("UPDATE presets SET crossover_freq = ? WHERE name = ?", 
    [frequency.toString(), presetName], 
    function(err) {
      if (err) {
        return res.status(500).json({ error: err.message });
      }
      
      if (this.changes === 0) {
        return res.status(404).json({ error: 'Preset not found' });
      }
      
      broadcast({ 
        event: 'crossover_updated', 
        preset: presetName,
        frequency 
      });
      
      res.json({ success: true, frequency });
    }
  );
});

// Preset Management
app.get('/presets', (req, res) => {
  db.all("SELECT name, is_current FROM presets ORDER BY name", (err, rows) => {
    if (err) {
      return res.status(500).json({ error: err.message });
    }
    const presets = rows.map(row => ({
      name: row.name,
      isCurrent: Boolean(row.is_current)
    }));
    res.json(presets);
  });
});

app.delete('/preset', (req, res) => {
  const name = req.query.name || req.body.name;
  
  if (!name) {
    return res.status(400).json({ error: 'Preset name is required' });
  }

  // First, delete associated EQ configurations
  db.run("DELETE FROM eq_configs WHERE preset_name = ?", [name], function(eqErr) {
    if (eqErr) {
      console.error("Error deleting eq_configs for preset:", name, eqErr.message);
      return res.status(500).json({ error: `Failed to delete associated EQ configs: ${eqErr.message}` });
    }

    // Then, delete the preset itself
    db.run("DELETE FROM presets WHERE name = ?", [name], function(presetErr) {
      if (presetErr) {
        return res.status(500).json({ error: `Failed to delete preset: ${presetErr.message}` });
      }

      if (this.changes === 0) {
        return res.status(404).json({ error: 'Preset not found' });
      }

      broadcast({ event: 'preset', action: 'deleted', name });
      res.json({ success: true, name });
    });
  });
});

app.get('/preset', (req, res) => {
  const name = req.query.name || 'Default';
  
  db.get("SELECT * FROM presets WHERE name = ?", [name], (err, preset) => {
    if (err) {
      return res.status(500).json({ error: err.message });
    }
    if (!preset) {
      return res.status(404).json({ error: 'Preset not found' });
    }
    
    // Get EQ configurations
    db.all("SELECT type,spl,peq_data FROM eq_configs WHERE preset_name = ? ORDER BY spl DESC", [name], (err, eqRows) => {
      console.log(name, eqRows);
      if (err) {
        return res.status(500).json({ error: err.message });
      }
      
      const roomCorrection = [];
      const preferenceEQ = [];
      
      eqRows.forEach(row => {
        console.log('Processing row:', row);
        try {
          if (row.peq_data) {
            console.log('Parsing peq_data:', row.peq_data);
            const peqData = typeof row.peq_data === 'string' ? JSON.parse(row.peq_data) : row.peq_data;
            const eqData = {
              spl: row.spl,
              peqs: peqData
            };
            
            if (row.type === 'room') {
              roomCorrection.push(eqData);
            } else if (row.type === 'pref') {
              preferenceEQ.push(eqData);
            }
          } else {
            console.log('Skipping row with no peq_data:', row);
          }
        } catch (e) {
          console.error('Error parsing PEQ data for row:', row);
          console.error('Error:', e);
        }
      });
      
      res.json({
        name: preset.name,
        isCurrent: Boolean(preset.is_current),
        isFIREnabled: Boolean(preset.is_fir_enabled),
        firFiles: ['couch_left', 'couch_right', 'couch_sub', 'dining_left', 'dining_right', 'dining_sub'],
        firLeft: preset.fir_left,
        firRight: preset.fir_right,
        firSub: preset.fir_sub,
        isSpeakerDelayEnabled: Boolean(preset.is_speaker_delay_enabled),
        speakerDelays: JSON.parse(preset.speaker_delays),
        isCrossoverEnabled: Boolean(preset.is_crossover_enabled),
        crossoverFreq: preset.crossover_freq,
        isPreferenceEQEnabled: Boolean(preset.is_preference_eq_enabled),
        gains: preset.gains ? JSON.parse(preset.gains) : { left: 1.0, right: 1.0, sub: 1.0 },
        preferenceEQ,
      });
    });
  });
});

app.post('/preset', (req, res) => {
  const action = req.query.action || req.body.action;
  
  if (action === 'create') {
    const name = req.query.name || req.body.name;
    
    if (!name) {
      return res.status(400).json({ error: 'Preset name is required' });
    }

    const defaultCrossoverFreq = '80';
    const defaultSpeakerDelays = JSON.stringify({ left: 0, right: 0, sub: 0 });
    const defaultIsCurrent = 1; // true, new presets are current by default
    const defaultIsFIREnabled = 0; // false
    const defaultIsSpeakerDelayEnabled = 0; // false
    const defaultIsCrossoverEnabled = 1; // true
    const defaultIsPreferenceEQEnabled = 0; // false

    // Default PEQ points: 3 points with gain 0, Q 1.0, at 100Hz, 1kHz, 10kHz
    const defaultPEQPoints = [
      { type: 'PK', freq: 100, gain: 0, q: 1.0, enabled: true },
      { type: 'PK', freq: 1000, gain: 0, q: 1.0, enabled: true },
      { type: 'PK', freq: 10000, gain: 0, q: 1.0, enabled: true }
    ];

    db.run('BEGIN TRANSACTION', function(err) {
      if (err) {
        console.error('Failed to start transaction:', err.message);
        return res.status(500).json({ error: `Failed to start transaction: ${err.message}` });
      }

      //Update any existing current preset to not be current
      db.run("UPDATE presets SET is_current = 0 WHERE is_current = 1");

      db.run("INSERT INTO presets (name, crossover_freq, speaker_delays, is_current, is_speaker_delay_enabled, is_crossover_enabled, is_fir_enabled, is_preference_eq_enabled) VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
        [name, defaultCrossoverFreq, defaultSpeakerDelays, defaultIsCurrent, defaultIsSpeakerDelayEnabled, defaultIsCrossoverEnabled, defaultIsFIREnabled, defaultIsPreferenceEQEnabled],
        function(err) {
          if (err) {
            db.run('ROLLBACK');
            if (err.code === 'SQLITE_CONSTRAINT') { // Preset name is unique
              return res.status(400).json({ error: 'Preset already exists' });
            }
            console.error('Failed to create preset:', err.message);
            return res.status(500).json({ error: `Failed to create preset: ${err.message}` });
          }
          const presetId = this.lastID;

          db.run("INSERT INTO eq_configs (preset_name, type, spl, peq_data) VALUES (?, 'room', 0, ?)",
            [name, JSON.stringify(defaultPEQPoints)],
            function(err) {
              if (err) {
                db.run('ROLLBACK');
                console.error('Failed to add room correction settings:', err.message);
                return res.status(500).json({ error: `Failed to add room correction settings: ${err.message}` });
              }

              db.run("INSERT INTO eq_configs (preset_name, type, spl, peq_data) VALUES (?, 'pref', 0, ?)",
                [name, JSON.stringify(defaultPEQPoints)],
                function(err) {
                  if (err) {
                    db.run('ROLLBACK');
                    console.error('Failed to add preference curve settings:', err.message);
                    return res.status(500).json({ error: `Failed to add preference curve settings: ${err.message}` });
                  }

                  db.run('COMMIT', function(err) {
                    if (err) {
                      // SQLite automatically rolls back the transaction if COMMIT fails.
                      console.error('Failed to commit transaction:', err.message);
                      return res.status(500).json({ error: `Failed to commit transaction: ${err.message}` });
                    }
                    broadcast({ event: 'preset', action: 'created', name, id: presetId });
                    const newPresetForResponse = {
                      name,
                      isCurrent: Boolean(defaultIsCurrent),
                      firLeft: '',
                      firRight: '',
                      firSub: '',
                      firFiles: ['couch_left', 'couch_right', 'couch_sub', 'dining_left', 'dining_right', 'dining_sub'],
                      isFIREnabled: Boolean(defaultIsFIREnabled),
                      isSpeakerDelayEnabled: Boolean(defaultIsSpeakerDelayEnabled),
                      speakerDelays: JSON.parse(defaultSpeakerDelays),
                      isCrossoverEnabled: Boolean(defaultIsCrossoverEnabled),
                      crossoverFreq: defaultCrossoverFreq,
                      isPreferenceEQEnabled: Boolean(defaultIsPreferenceEQEnabled),
                      gains: { left: 1.0, right: 1.0, sub: 1.0 },
                      preferenceEQ: [{ spl: 0, peqs: defaultPEQPoints }],
                    }
                    res.status(201).json(newPresetForResponse);
                  });
                }
              );
            }
          );
        }
      );
    });
  } else if (action === 'copy') {
    const sourceName = req.query.source || req.body.source;
    const newName = req.query.name || req.body.name;
    
    if (!sourceName || !newName) {
      return res.status(400).json({ error: 'Source and new preset names are required' });
    }
    
    db.get("SELECT * FROM presets WHERE name = ?", [sourceName], (err, source) => {
      if (err) {
        return res.status(500).json({ error: err.message });
      }
      if (!source) {
        return res.status(404).json({ error: 'Source preset not found' });
      }
      
      // First, unset all current presets
      db.run("UPDATE presets SET is_current = 0", (err) => {
        if (err) {
          return res.status(500).json({ error: err.message });
        }
        
        db.run(`INSERT INTO presets (name, speaker_delays, crossover_freq, is_current, is_speaker_delay_enabled, is_crossover_enabled, is_fir_enabled, is_preference_eq_enabled, fir_left, fir_right, fir_sub, gains) 
                VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)`, 
               [newName, source.speaker_delays, source.crossover_freq, 1, source.is_speaker_delay_enabled, source.is_crossover_enabled, source.is_fir_enabled, source.is_preference_eq_enabled, source.fir_left, source.fir_right, source.fir_sub, source.gains], 
               function(err) {
          if (err) {
            if (err.code === 'SQLITE_CONSTRAINT') {
              return res.status(400).json({ error: 'Preset already exists' });
            }
            return res.status(500).json({ error: err.message });
          }
          
          // Copy EQ configurations
          db.all("SELECT type, spl, peq_data FROM eq_configs WHERE preset_name = ?", [sourceName], (err, eqRows) => {
            if (err) {
              return res.status(500).json({ error: err.message });
            }
            
            const stmt = db.prepare("INSERT INTO eq_configs (preset_name, type, spl, peq_data) VALUES (?, ?, ?, ?)");
            eqRows.forEach(row => {
              stmt.run(newName, row.type, row.spl, row.peq_data);
            });
            stmt.finalize();

            broadcast({ event: 'preset', action: 'copied', source: sourceName, name: newName });
            res.json({ success: true, name: newName });
          });
        });
      });
    });
  } else {
    res.status(400).json({ error: 'Invalid action. Must be "create" or "copy"' });
  }
});

app.put('/preset', (req, res) => {
  const action = req.query.action || req.body.action;
  
  if (action === 'rename') {
    const oldName = req.query.old || req.body.old;
    const newName = req.query.new || req.body.new;
    
    if (!oldName || !newName) {
      return res.status(400).json({ error: 'Old and new preset names are required' });
    }
    
    db.run("UPDATE presets SET name = ? WHERE name = ?", [newName, oldName], function(err) {
      if (err) {
        if (err.code === 'SQLITE_CONSTRAINT') {
          return res.status(400).json({ error: 'New preset name already exists' });
        }
        return res.status(500).json({ error: err.message });
      }
      
      if (this.changes === 0) {
        return res.status(404).json({ error: 'Preset not found' });
      }
      
      // Update EQ configurations
      db.run("UPDATE eq_configs SET preset_name = ? WHERE preset_name = ?", [newName, oldName], (err) => {
        if (err) {
          return res.status(500).json({ error: err.message });
        }
        
        broadcast({ event: 'preset', action: 'renamed', oldName, newName });
        res.json({ success: true, name: newName });
      });
    });
  } else {
    res.status(400).json({ error: 'Invalid action. Must be "rename"' });
  }
});

// Backup and Restore
app.get('/backup', (req, res) => {
  // Create a backup of the database
  res.setHeader('Content-Type', 'application/msgpack');
  res.setHeader('Content-Disposition', 'attachment; filename="vybes_config.msgpack"');
  
  // For this mock, we'll just return a simple JSON backup
  db.all("SELECT * FROM system_settings", (err, settings) => {
    if (err) {
      return res.status(500).json({ error: err.message });
    }
    
    db.all("SELECT * FROM presets", (err, presets) => {
      if (err) {
        return res.status(500).json({ error: err.message });
      }
      
      db.all("SELECT * FROM eq_configs", (err, eqConfigs) => {
        if (err) {
          return res.status(500).json({ error: err.message });
        }
        
        const backup = {
          settings,
          presets,
          eqConfigs
        };
        
        res.json(backup);
      });
    });
  });
});

app.post('/restore', (req, res) => {
  // Handle file upload for restore
  // This is a simplified version - in practice you'd handle multipart/form-data
  const backupData = req.body;
  
  if (!backupData) {
    return res.status(400).json({ error: 'No backup data provided' });
  }
  
  // Restore would involve parsing the backup file and updating the database
  // For this mock, we'll just return success
  res.json({ success: true, message: 'Configuration restored successfully' });
});

// Serve static assets and SPA fallback
app.use('/assets', express.static(path.join(__dirname, '..', 'WebUI', 'dist', 'assets')));
app.use('/images', express.static(path.join(__dirname, '..', 'WebUI', 'dist', 'images')));

// Serve index.html for all other routes (SPA fallback)
app.get('*', (req, res) => {
  res.sendFile(path.join(__dirname, '..', 'WebUI', 'dist', 'index.html'));
});

// Error handling middleware
app.use((err, req, res, next) => {
  console.error(err.stack);
  res.status(500).json({ error: 'Internal server error' });
});

// Start server
app.listen(PORT, () => {
  console.log(`Vybes mock server running on port ${PORT}`);
  console.log(`Add "127.0.0.1 vybes.local" to your hosts file`);
  console.log(`WebSocket server running on port 8080`);
  console.log(`Database: ${dbPath}`);
});

// Graceful shutdown
process.on('SIGINT', () => {
  console.log('\nShutting down server...');
  db.close();
  process.exit(0);
});