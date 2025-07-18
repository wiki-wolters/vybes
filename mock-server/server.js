const express = require('express');
const sqlite3 = require('sqlite3').verbose();
const WebSocket = require('ws');
const cors = require('cors');
const path = require('path');

const app = express();
const PORT = 80; // Standard HTTP port for vybes.local

// Middleware
app.use(cors());
app.use(express.json());
app.use(express.static('public')); // Serve static files if needed

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

// ===== API ROUTES =====

// Get FIR filter files
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

// Set crossover frequency for a preset
app.put('/preset/:name/crossover/freq/:freq', (req, res) => {
  const presetName = decodeURIComponent(req.params.name);
  const frequency = parseInt(req.params.freq);
  
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

// Set FIR filter for a channel
app.put('/preset/:name/fir/file/:channel/:filter', (req, res) => {
  const presetName = decodeURIComponent(req.params.name);
  const channel = req.params.channel;
  const filterName = decodeURIComponent(req.params.filter);
  
  if (!['left', 'right', 'sub'].includes(channel)) {
    return res.status(400).json({ error: 'Invalid channel. Must be left, right, or sub' });
  }
  
  const column = `fir_${channel}`;
  db.run(`UPDATE presets SET ${column} = ? WHERE name = ?`,
    [filterName, presetName],
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
        filter: filterName
      });
      
      res.json({ success: true, channel, filter: filterName });
    }
  );
});

// Create or update an EQ set
app.put('/preset/:name/eq/pref/0', (req, res) => {
  const presetName = decodeURIComponent(req.params.name);
  const type = 'pref';
  const spl = 0;
  const peqPoints = req.body;

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

app.put('/speaker/:speaker/gain/:gain', async (req, res) => {
  const gain = parseFloat(req.params.gain);
  if (gain < 0 || gain > 2) {
    return res.status(400).json({ error: 'Gain must be between 0 and 2' });
  }
  const speaker = req.params.speaker;
  if (!['left', 'right', 'sub'].includes(speaker)) {
    return res.status(400).json({ error: 'Speaker must be "left", "right", or "sub"' });
  }
  
  try {
    await setSetting(`${speaker}_gain`, gain.toString());
    broadcast({ event: `${speaker}_gain`, gain });
    res.json({ success: true, gain });
  } catch (error) {
    res.status(500).json({ error: error.message });
  }
});

app.put('/mute/:state', async (req, res) => {
  const state = req.params.state;
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

app.put('/mute/percent/:percent', async (req, res) => {
  const percent = parseInt(req.params.percent);
  if (percent < 1 || percent > 100) {
    return res.status(400).json({ error: 'Percent must be between 1 and 100' });
  }
  
  try {
    await setSetting('mute_percent', percent.toString());
    broadcast({ event: 'mute_percent', percent });
    res.json({ success: true, percent });
  } catch (error) {
    res.status(500).json({ error: error.message });
  }
});

app.put('/gains/input', async (req, res) => {
  const { bluetooth, spdif, tone } = req.body;

  if (bluetooth === undefined || spdif === undefined || tone === undefined) {
    return res.status(400).json({ error: 'Missing gain values' });
  }

  try {
    await setSetting('bluetooth_gain', bluetooth.toString());
    await setSetting('spdif_gain', spdif.toString());
    await setSetting('tone_gain', tone.toString());

    broadcast({ event: 'input_gains', gains: { bluetooth, spdif, tone } });
    res.json({ success: true, gains: { bluetooth, spdif, tone } });
  } catch (error) {
    res.status(500).json({ error: error.message });
  }
});

// Preset management
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

app.get('/preset/:name', (req, res) => {
  const name = decodeURIComponent(req.params.name);
  
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
        try {
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
        } catch (e) {
          console.error('Error parsing PEQ data:', e);
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
        preferenceEQ,
      });
    });
  });
});

app.post('/preset/create/:name', (req, res) => {
  const name = decodeURIComponent(req.params.name);

  const defaultCrossoverFreq = '80';
  const defaultSpeakerDelays = JSON.stringify({ left: 0, right: 0, sub: 0 });
  const defaultIsCurrent = 1; // false, new presets are not current by default
  const defaultIsFIREnabled = 0; // false
  const defaultIsSpeakerDelayEnabled = 0; // false
  const defaultIsCrossoverEnabled = 1; // true
  const defaultIsPreferenceEQEnabled = 0; // false

  // Default PEQ points: 3 points with gain 0, Q 1.0, at 100Hz, 1kHz, 10kHz
  // Assuming type PK (parametric) and enabled for each point.
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
                    speakerDelays: defaultSpeakerDelays,
                    isCrossoverEnabled: Boolean(defaultIsCrossoverEnabled),
                    crossoverFreq: defaultCrossoverFreq,
                    isPreferenceEQEnabled: Boolean(defaultIsPreferenceEQEnabled),
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
});

app.post('/preset/copy/:source/:new', (req, res) => {
  const sourceName = decodeURIComponent(req.params.source);
  const newName = decodeURIComponent(req.params.new);
  
  db.get("SELECT * FROM presets WHERE name = ?", [sourceName], (err, source) => {
    if (err) {
      return res.status(500).json({ error: err.message });
    }
    if (!source) {
      return res.status(404).json({ error: 'Source preset not found' });
    }
    
    db.run(`INSERT INTO presets (name, speaker_delays, crossover_freq, is_current, is_speaker_delay_enabled, is_crossover_enabled, is_fir_enabled, is_preference_eq_enabled, fir_left, fir_right, fir_sub) 
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)`, 
           [newName, source.speaker_delays, source.crossover_freq, 1, source.is_speaker_delay_enabled, source.is_crossover_enabled, source.is_fir_enabled, source.is_preference_eq_enabled, source.fir_left, source.fir_right, source.fir_sub], 
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

        //Source is_current must be updated to zero
        db.run("UPDATE presets SET is_current = 0 WHERE name = ?", [sourceName], function(err) {
          if (err) {
            return res.status(500).json({ error: err.message });
          }
          if (this.changes === 0) {
            return res.status(404).json({ error: 'Source preset not found' });
          }

          broadcast({ event: 'preset', action: 'copied', source: sourceName, name: newName });
          res.json({ success: true, name: newName });
        });
      });
    });
  });
});

app.put('/preset/rename/:old/:new', (req, res) => {
  const oldName = decodeURIComponent(req.params.old);
  const newName = decodeURIComponent(req.params.new);
  
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
});

app.delete('/preset/:name', (req, res) => {
  const name = decodeURIComponent(req.params.name);

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

// Toggle speaker delay
app.put('/preset/:name/delay/enabled/:state', (req, res) => {
  const state = req.params.state;
  const name  = req.params.name;
  
  if (!['on', 'off'].includes(state)) {
    return res.status(400).json({ error: 'State must be "on" or "off"' });
  }
  
  const delayEnabled = state === 'on' ? 1 : 0;
  
  db.run("UPDATE presets SET is_speaker_delay_enabled = ? WHERE name = ?", [delayEnabled, name], function(err) {
    if (err) {
      return res.status(500).json({ error: err.message });
    }
    
    if (this.changes === 0) {
      return res.status(404).json({ error: 'Preset not found' });
    }
    
    broadcast({ event: 'is_speaker_delay_enabled', name, state });
    res.json({ success: true, name, state });
  });
});

// Toggle EQ
app.put('/preset/:name/eq/:type/enabled/:state', (req, res) => {
  const presetName = decodeURIComponent(req.params.name);
  const type = req.params.type;
  const state = req.params.state;
  
  if (!['pref'].includes(type)) {
    return res.status(400).json({ error: 'Type must be "pref"' });
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



// Toggle crossover
app.put('/preset/:name/crossover/enabled/:state', (req, res) => {
  const presetName = decodeURIComponent(req.params.name);
  const state = req.params.state;
  
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

// Crossover
app.put('/preset/:name/crossover/:freq/:slope', (req, res) => {
  const presetName = decodeURIComponent(req.params.name);
  const frequency = parseInt(req.params.freq);
  const slope = req.params.slope;
  
  if (frequency < 40 || frequency > 150) {
    return res.status(400).json({ error: 'Frequency must be between 40 and 150 Hz' });
  }
  if (!['12', '24'].includes(slope)) {
    return res.status(400).json({ error: 'Slope must be "12" or "24"' });
  }
  
  const crossover = JSON.stringify({ frequency, slope });
  
  db.run("UPDATE presets SET crossover = ? WHERE name = ?", [crossover, presetName], function(err) {
    if (err) {
      return res.status(500).json({ error: err.message });
    }
    
    if (this.changes === 0) {
      return res.status(404).json({ error: 'Preset not found' });
    }
    
    broadcast({ event: 'crossover', preset: presetName, frequency, slope });
    res.json({ success: true, preset: presetName, frequency, slope });
  });
});

// System status - GET endpoint
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
        tone: toneGain ? parseFloat(toneGain) : 0
      },
      currentPreset
    });
  } catch (error) {
    res.status(500).json({ error: error.message });
  }
});

// Set active preset
app.put('/preset/active/:name', (req, res) => {
  const name = decodeURIComponent(req.params.name);
  
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

// Update speaker delay for specific preset (the existing one works with current preset)
// This version allows specifying the preset name
app.put('/preset/:name/delay/:speaker/:us', (req, res) => {
  const presetName = decodeURIComponent(req.params.name);
  const speaker = req.params.speaker;
  const delayUs = parseFloat(req.params.us);
  
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

//Enable FIR filters
app.put('/preset/:name/fir/enabled/:state', (req, res) => {
  const name = decodeURIComponent(req.params.name);
  const state = req.params.state === 'on' ? 1 : 0;
  
  db.run("UPDATE presets SET is_fir_enabled = ? WHERE name = ?", [state, name], function(err) {
    if (err) {
      return res.status(500).json({ error: err.message });
    }
    
    if (this.changes === 0) {
      return res.status(404).json({ error: 'Preset not found' });
    }
    
    broadcast({ event: 'fir_enabled', preset: name });
    res.json({ success: true, preset: name });
  });
});

app.put('/generate/noise/:volume', (req, res) => {
  const volume = req.params.volume;
  broadcast({ event: 'noise', volume });
  res.json({ success: true, volume });
});

// Error handling middleware
app.use((err, req, res, next) => {
  console.error(err.stack);
  res.status(500).json({ error: 'Internal server error' });
});

// 404 handler
app.use((req, res) => {
  res.status(404).json({ error: 'Endpoint not found' });
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