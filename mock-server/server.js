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
    speaker_delays TEXT DEFAULT '{"left": 0, "right": 0, "sub": 0}',
    crossover TEXT DEFAULT '{"frequency": 80, "slope": "12"}',
    equal_loudness INTEGER DEFAULT 0,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP
  )`);

  // EQ configurations table
  db.run(`CREATE TABLE IF NOT EXISTS eq_configs (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    preset_name TEXT,
    type TEXT,
    spl INTEGER,
    peq_data TEXT,
    FOREIGN KEY (preset_name) REFERENCES presets (name) ON DELETE CASCADE
  )`);

  // Insert default settings if they don't exist
  db.get("SELECT value FROM system_settings WHERE key = 'subwoofer_state'", (err, row) => {
    if (!row) {
      const defaultSettings = [
        ['calibration_spl', '85'],
        ['subwoofer_state', 'on'],
        ['bypass_state', 'off'],
        ['mute_state', 'off'],
        ['mute_percent', '0'],
        ['tone_frequency', '1000'],
        ['tone_volume', '50'],
        ['noise_volume', '0']
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

// Calibration
app.put('/calibrate/:spl', async (req, res) => {
  const spl = parseInt(req.params.spl);
  if (spl < 40 || spl > 120) {
    return res.status(400).json({ error: 'SPL must be between 40 and 120' });
  }
  
  try {
    await setSetting('calibration_spl', spl.toString());
    broadcast({ event: 'calibration', spl });
    res.json({ success: true, spl });
  } catch (error) {
    res.status(500).json({ error: error.message });
  }
});

// System controls
app.put('/sub/:state', async (req, res) => {
  const state = req.params.state;
  if (!['on', 'off'].includes(state)) {
    return res.status(400).json({ error: 'State must be "on" or "off"' });
  }
  
  try {
    await setSetting('subwoofer_state', state);
    broadcast({ event: 'subwoofer', state });
    res.json({ success: true, state });
  } catch (error) {
    res.status(500).json({ error: error.message });
  }
});

app.put('/bypass/:state', async (req, res) => {
  const state = req.params.state;
  if (!['on', 'off'].includes(state)) {
    return res.status(400).json({ error: 'State must be "on" or "off"' });
  }
  
  try {
    await setSetting('bypass_state', state);
    broadcast({ event: 'bypass', state });
    res.json({ success: true, state });
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

// Tone generation
app.put('/generate/tone/:freq/:volume', async (req, res) => {
  const freq = parseInt(req.params.freq);
  const volume = parseInt(req.params.volume);
  
  if (freq < 10 || freq > 20000) {
    return res.status(400).json({ error: 'Frequency must be between 10 and 20000 Hz' });
  }
  if (volume < 1 || volume > 100) {
    return res.status(400).json({ error: 'Volume must be between 1 and 100' });
  }
  
  try {
    await setSetting('tone_frequency', freq.toString());
    await setSetting('tone_volume', volume.toString());
    broadcast({ event: 'tone', frequency: freq, volume });
    res.json({ success: true, frequency: freq, volume });
  } catch (error) {
    res.status(500).json({ error: error.message });
  }
});

app.put('/generate/noise/:volume', async (req, res) => {
  const volume = parseInt(req.params.volume);
  if (volume < 0 || volume > 100) {
    return res.status(400).json({ error: 'Volume must be between 0 and 100' });
  }
  
  try {
    await setSetting('noise_volume', volume.toString());
    broadcast({ event: 'noise', volume });
    res.json({ success: true, volume });
  } catch (error) {
    res.status(500).json({ error: error.message });
  }
});

app.put('/pulse', (req, res) => {
  broadcast({ event: 'pulse', timestamp: Date.now() });
  res.json({ success: true, message: 'Playing test pulse' });
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
        const eqData = {
          spl: row.spl,
          peqSet: JSON.parse(row.peq_data)
        };
        
        if (row.type === 'room') {
          roomCorrection.push(eqData);
        } else if (row.type === 'pref') {
          preferenceEQ.push(eqData);
        }
      });
      
      res.json({
        name: preset.name,
        isCurrent: Boolean(preset.is_current),
        speakerDelays: JSON.parse(preset.speaker_delays),
        crossover: JSON.parse(preset.crossover),
        roomCorrection,
        preferenceEQ,
        equalLoudness: Boolean(preset.equal_loudness)
      });
    });
  });
});

app.post('/preset/create/:name', (req, res) => {
  const name = decodeURIComponent(req.params.name);

  const defaultCrossover = JSON.stringify({ subwoofer: { frequency: 40, enabled: true } });
  const defaultSpeakerDelays = JSON.stringify({ left: 0, right: 0, sub: 0 });
  const defaultEqualLoudness = 0; // false
  const defaultIsCurrent = 1; // false, new presets are not current by default

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

    db.run("INSERT INTO presets (name, crossover, speaker_delays, equal_loudness, is_current) VALUES (?, ?, ?, ?, ?)",
      [name, defaultCrossover, defaultSpeakerDelays, defaultEqualLoudness, defaultIsCurrent],
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
                    speakerDelays: defaultSpeakerDelays,
                    crossover: defaultCrossover,
                    roomCorrection: [{ spl: 0, peqs: defaultPEQPoints }],
                    preferenceEQ: [{ spl: 0, peqs: defaultPEQPoints }],
                    equalLoudness: Boolean(defaultEqualLoudness)
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
    
    db.run(`INSERT INTO presets (name, speaker_delays, crossover, equal_loudness) 
            VALUES (?, ?, ?, ?)`, 
           [newName, source.speaker_delays, source.crossover, source.equal_loudness], 
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

// Speaker delays
app.put('/preset/delay/:speaker/:ms', (req, res) => {
  const speaker = req.params.speaker;
  const delayMs = parseFloat(req.params.ms);
  
  if (!['left', 'right', 'sub'].includes(speaker)) {
    return res.status(400).json({ error: 'Speaker must be "left", "right", or "sub"' });
  }
  
  // Get current preset
  db.get("SELECT name, speaker_delays FROM presets WHERE is_current = 1", (err, preset) => {
    if (err) {
      return res.status(500).json({ error: err.message });
    }
    if (!preset) {
      return res.status(400).json({ error: 'No current preset selected' });
    }
    
    const delays = JSON.parse(preset.speaker_delays);
    delays[speaker] = delayMs;
    
    db.run("UPDATE presets SET speaker_delays = ? WHERE name = ?", [JSON.stringify(delays), preset.name], (err) => {
      if (err) {
        return res.status(500).json({ error: err.message });
      }
      
      broadcast({ event: 'speaker_delay', speaker, delayMs, preset: preset.name });
      res.json({ success: true, speaker, delayMs });
    });
  });
});

// EQ management
app.post('/preset/:name/eq/:type/:spl', (req, res) => {
  const presetName = decodeURIComponent(req.params.name);
  const type = req.params.type;
  const spl = parseInt(req.params.spl);
  const peqSet = req.body;
  
  if (!['room', 'pref'].includes(type)) {
    return res.status(400).json({ error: 'Type must be "room" or "pref"' });
  }
  if (spl < 0 || spl > 120) {
    return res.status(400).json({ error: 'SPL must be between 0 and 120' });
  }
  if (!Array.isArray(peqSet)) {
    return res.status(400).json({ error: 'Body must be an array of PEQ points' });
  }
  
  // Validate PEQ points
  for (let i = 0; i < peqSet.length; i++) {
    const point = peqSet[i];
    if (!point.frequency || !point.gain || !point.q) {
      return res.status(400).json({ error: `PEQ point ${i} must have frequency, gain, and q properties` });
    }
  }
  
  db.run("INSERT OR REPLACE INTO eq_configs (preset_name, type, spl, peq_data) VALUES (?, ?, ?, ?)", 
         [presetName, type, spl, JSON.stringify(peqSet)], (err) => {
    if (err) {
      return res.status(500).json({ error: err.message });
    }
    
    broadcast({ event: 'eq', preset: presetName, type, spl, peqSet });
    res.json({ success: true, preset: presetName, type, spl });
  });
});

app.delete('/preset/:name/eq/:type/:spl', (req, res) => {
  const presetName = decodeURIComponent(req.params.name);
  const type = req.params.type;
  const spl = parseInt(req.params.spl);
  
  db.run("DELETE FROM eq_configs WHERE preset_name = ? AND type = ? AND spl = ?", 
         [presetName, type, spl], function(err) {
    if (err) {
      return res.status(500).json({ error: err.message });
    }
    
    if (this.changes === 0) {
      return res.status(404).json({ error: 'EQ configuration not found' });
    }
    
    broadcast({ event: 'eq_deleted', preset: presetName, type, spl });
    res.json({ success: true, preset: presetName, type, spl });
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

// Equal loudness
app.put('/preset/:name/equal-loudness/:state', (req, res) => {
  const presetName = decodeURIComponent(req.params.name);
  const state = req.params.state;
  
  if (!['on', 'off'].includes(state)) {
    return res.status(400).json({ error: 'State must be "on" or "off"' });
  }
  
  const equalLoudness = state === 'on' ? 1 : 0;
  
  db.run("UPDATE presets SET equal_loudness = ? WHERE name = ?", [equalLoudness, presetName], function(err) {
    if (err) {
      return res.status(500).json({ error: err.message });
    }
    
    if (this.changes === 0) {
      return res.status(404).json({ error: 'Preset not found' });
    }
    
    broadcast({ event: 'equal_loudness', preset: presetName, state });
    res.json({ success: true, preset: presetName, state });
  });
});

// Calibration - GET endpoint
app.get('/calibration', async (req, res) => {
  try {
    const calibrationSpl = await getSetting('calibration_spl');
    const isCalibrated = calibrationSpl !== null;
    
    res.json({
      isCalibrated,
      spl: calibrationSpl ? parseInt(calibrationSpl) : null
    });
  } catch (error) {
    res.status(500).json({ error: error.message });
  }
});

// System status - GET endpoint
app.get('/status', async (req, res) => {
  try {
    const [
      calibrationSpl,
      subwooferState,
      bypassState,
      muteState,
      mutePercent,
      toneFrequency,
      toneVolume,
      noiseVolume
    ] = await Promise.all([
      getSetting('calibration_spl'),
      getSetting('subwoofer_state'),
      getSetting('bypass_state'),
      getSetting('mute_state'),
      getSetting('mute_percent'),
      getSetting('tone_frequency'),
      getSetting('tone_volume'),
      getSetting('noise_volume')
    ]);

    // Get current preset
    const currentPreset = await new Promise((resolve, reject) => {
      db.get("SELECT name FROM presets WHERE is_current = 1", (err, row) => {
        if (err) reject(err);
        else resolve(row ? row.name : null);
      });
    });

    res.json({
      calibration: {
        isCalibrated: calibrationSpl !== null,
        spl: calibrationSpl ? parseInt(calibrationSpl) : null
      },
      subwoofer: subwooferState || 'on',
      bypass: bypassState || 'off',
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
      currentPreset
    });
  } catch (error) {
    res.status(500).json({ error: error.message });
  }
});

// Stop tone generation
app.put('/generate/tone/stop', (req, res) => {
  broadcast({ event: 'tone', frequency: 0, volume: 0, stopped: true });
  res.json({ success: true, message: 'Tone generation stopped' });
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
app.put('/preset/:name/delay/:speaker/:ms', (req, res) => {
  const presetName = decodeURIComponent(req.params.name);
  const speaker = req.params.speaker;
  const delayMs = parseFloat(req.params.ms);
  
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
    delays[speaker] = delayMs;
    
    db.run("UPDATE presets SET speaker_delays = ? WHERE name = ?", [JSON.stringify(delays), presetName], (err) => {
      if (err) {
        return res.status(500).json({ error: err.message });
      }
      
      broadcast({ event: 'speaker_delay', speaker, delayMs, preset: presetName });
      res.json({ success: true, speaker, delayMs, preset: presetName });
    });
  });
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