const express = require('express');
const sqlite3 = require('sqlite3').verbose();
const WebSocket = require('ws');
const cors = require('cors');
const path = require('path');

const app = express();
const expressStaticGzip = require('express-static-gzip');
const PORT = process.env.PORT !== undefined ? parseInt(process.env.PORT, 10) : 80; // Standard HTTP port for vybes.local
// Standalone websocket port (the real device serves /live-updates on the
// same port; the mock keeps it separate so vite can proxy-free connect).
const WS_PORT = process.env.WS_PORT !== undefined ? parseInt(process.env.WS_PORT, 10) : 8080;

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

// Initialize SQLite database (VYBES_DB_PATH lets tests point at an isolated
// throwaway file so runs are hermetic)
const dbPath = process.env.VYBES_DB_PATH || path.join(__dirname, 'vybes.db');
const db = new sqlite3.Database(dbPath);

// Mirrors MAX_PEQ_POINTS in ESP/esp-web-server/config.h
const MAX_PEQ_POINTS = 15;

const clamp = (value, lo, hi) => Math.min(hi, Math.max(lo, value));

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
    gains TEXT DEFAULT '{"left": 100, "right": 100, "sub": 100}',
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

  // Insert default settings if they don't exist. Gains use the same scales
  // as the ESP API: speaker gains are 0-100 percent (api_system.cpp reports
  // them x100), input gains are linear 0.0-1.0 (passed through verbatim).
  db.get("SELECT value FROM system_settings WHERE key = 'sub_gain'", (err, row) => {
    if (!row) {
      const defaultSettings = [
        ['sub_gain', '100'],
        ['left_gain', '100'],
        ['right_gain', '100'],
        ['mute_state', 'off'],
        ['mute_percent', '100'],
        ['tone_frequency', '1000'],
        ['tone_volume', '50'],
        ['noise_volume', '0'],
        ['bluetooth_gain', '1.0'],
        ['spdif_gain', '0.0'],
        ['usb_gain', '1.0'],
        ['tone_gain', '0.0'],
        ['analog_gain', '1.0'],
        ['volume', '50']
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

  // Migrate databases seeded by older mock versions, which stored speaker
  // and preset gains on the ESP-internal 0-1 linear scale (the API talks
  // 0-100 percent - see ESP/esp-web-server/config.cpp) and seeded a
  // mute_percent of 0.
  ['sub_gain', 'left_gain', 'right_gain'].forEach((key) => {
    db.get("SELECT value FROM system_settings WHERE key = ?", [key], (err, row) => {
      if (!err && row && parseFloat(row.value) <= 2) {
        db.run("UPDATE system_settings SET value = ? WHERE key = ?",
          [(parseFloat(row.value) * 100).toString(), key]);
      }
    });
  });
  db.get("SELECT value FROM system_settings WHERE key = 'mute_percent'", (err, row) => {
    if (!err && row && parseInt(row.value) === 0) {
      db.run("UPDATE system_settings SET value = '100' WHERE key = 'mute_percent'");
    }
  });
  db.all("SELECT name, gains FROM presets", (err, rows) => {
    if (err || !rows) return;
    rows.forEach((row) => {
      try {
        const gains = JSON.parse(row.gains);
        if ([gains.left, gains.right, gains.sub].every((v) => typeof v === 'number' && v <= 2)) {
          const scaled = JSON.stringify({
            left: gains.left * 100,
            right: gains.right * 100,
            sub: gains.sub * 100
          });
          db.run("UPDATE presets SET gains = ? WHERE name = ?", [scaled, row.name]);
        }
      } catch (e) { /* leave unparseable rows alone */ }
    });
  });
});

// WebSocket server for live updates
const wss = new WebSocket.Server({ port: WS_PORT });
wss.on('listening', () => {
  // Report the actual port (WS_PORT=0 asks the OS for an ephemeral one)
  console.log(`WebSocket server running on port ${wss.address().port}`);
});

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

  ws.on('message', (msg) => {
    // The analyzer page sends this while open; it keeps mock RTA frames flowing
    if (msg.toString() === 'rta:keepalive') {
      rtaLastKeepaliveAt = Date.now();
    }
  });

  ws.on('close', () => {
    console.log('WebSocket client disconnected');
  });
});

// --- Mock RTA streaming ---
// Streams synthesized 31-band spectrum frames in the same format as the
// real device ("{type:'rta', d:'<62 hex chars>'}") while a client's
// rta:keepalive is fresh. Shape: pink-ish tilt, a slowly wandering bump,
// and some per-band wobble so the UI visibly animates.
const RTA_BAND_CENTERS = [
  20, 25, 31.5, 40, 50, 63, 80, 100, 125, 160, 200, 250, 315, 400, 500,
  630, 800, 1000, 1250, 1600, 2000, 2500, 3150, 4000, 5000, 6300, 8000,
  10000, 12500, 16000, 20000
];
let rtaLastKeepaliveAt = 0;

function mockRtaFrameHex(t) {
  let hex = '';
  for (let i = 0; i < RTA_BAND_CENTERS.length; i++) {
    const fc = RTA_BAND_CENTERS[i];
    const bumpCenter = 2 + 0.6 * Math.sin(t / 4000); // log10(freq) of the bump
    const dB = -28
      - 7 * Math.log10(fc / 20)
      + 8 * Math.exp(-((Math.log10(fc) - bumpCenter) ** 2) / 0.06)
      + 2.5 * Math.sin(t / 600 + i * 1.7);
    const v = Math.max(0, Math.min(255, Math.round((dB + 100) * 2)));
    hex += v.toString(16).padStart(2, '0');
  }
  return hex;
}

setInterval(() => {
  if (Date.now() - rtaLastKeepaliveAt > 5000) return;
  broadcast({ type: 'rta', d: mockRtaFrameHex(Date.now()) });
}, 100);

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

// ===== API ROUTES - MATCHING THE ESP32 ROUTES =====
// Each route mirrors its handler in ESP/esp-web-server (web_server.cpp lists
// the routes; api_*.cpp implement them): same query parameter names, same
// value ranges, same response and websocket broadcast shapes.

// System Status - shape matches api_system.cpp handleGetStatus
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
      toneGain,
      analogGain,
      volume
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
      getSetting('tone_gain'),
      getSetting('analog_gain'),
      getSetting('volume')
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
        // 0-100 percent, like the ESP (stored linear, reported x100)
        left: leftGain ? parseFloat(leftGain) : 100,
        right: rightGain ? parseFloat(rightGain) : 100,
        sub: subGain ? parseFloat(subGain) : 100
      },
      inputGains: {
        spdif: spdifGain ? parseFloat(spdifGain) : 0,
        bluetooth: bluetoothGain ? parseFloat(bluetoothGain) : 0,
        usb: usbGain ? parseFloat(usbGain) : 0,
        tone: toneGain ? parseFloat(toneGain) : 0,
        analog: analogGain ? parseFloat(analogGain) : 0
      },
      mute: {
        muted: muteState === 'on',
        percent: mutePercent ? parseInt(mutePercent) : 100
      },
      tone: {
        frequency: toneFrequency ? parseInt(toneFrequency) : 1000,
        volume: toneVolume ? parseInt(toneVolume) : 50
      },
      noise: {
        volume: noiseVolume ? parseInt(noiseVolume) : 0
      },
      currentPreset,
      volume: volume ? parseInt(volume) : 50
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

// Mute controls - api_system.cpp handlePutMutePercent (0-100)
app.put('/mute/percent', async (req, res) => {
  const percent = parseInt(req.query.percent);

  if (isNaN(percent) || percent < 0 || percent > 100) {
    return res.status(400).json({ error: 'Percent must be between 0 and 100' });
  }

  try {
    await setSetting('mute_percent', percent.toString());
    const payload = { messageType: 'mutePercentChanged', mutePercent: percent };
    broadcast(payload);
    res.json(payload);
  } catch (error) {
    res.status(500).json({ error: error.message });
  }
});

app.put('/mute', async (req, res) => {
  const state = req.query.state;

  if (!['on', 'off'].includes(state)) {
    return res.status(400).json({ error: 'State must be "on" or "off"' });
  }

  try {
    await setSetting('mute_state', state);
    const payload = { messageType: 'muteChanged', muted: state === 'on' };
    broadcast(payload);
    res.json(payload);
  } catch (error) {
    res.status(500).json({ error: error.message });
  }
});

// Signal generator - api_signal_generator.cpp (replies and broadcasts have
// no messageType field, just the values)
app.put('/generate/tone', async (req, res) => {
  const frequency = parseInt(req.query.frequency);
  const volume = parseInt(req.query.volume);

  if (isNaN(frequency) || frequency < 20 || frequency > 20000) {
    return res.status(400).json({ error: 'Frequency must be between 20 and 20000' });
  }
  if (isNaN(volume) || volume < 0 || volume > 100) {
    return res.status(400).json({ error: 'Volume must be between 0 and 100' });
  }

  try {
    await setSetting('tone_frequency', frequency.toString());
    await setSetting('tone_volume', volume.toString());
    const payload = { toneFrequency: frequency, toneVolume: volume };
    broadcast(payload);
    res.json(payload);
  } catch (error) {
    res.status(500).json({ error: error.message });
  }
});

app.put('/generate/tone/stop', async (req, res) => {
  try {
    // api_signal_generator.cpp handlePutToneStop zeroes both values
    await setSetting('tone_frequency', '0');
    await setSetting('tone_volume', '0');
    const payload = { toneFrequency: 0, toneVolume: 0 };
    broadcast(payload);
    res.json(payload);
  } catch (error) {
    res.status(500).json({ error: error.message });
  }
});

app.put('/noise', async (req, res) => {
  const level = parseInt(req.query.level);

  if (isNaN(level) || level < 0 || level > 100) {
    return res.status(400).json({ error: 'Level must be between 0 and 100' });
  }

  try {
    await setSetting('noise_volume', level.toString());
    const payload = { noiseVolume: level };
    broadcast(payload);
    res.json(payload);
  } catch (error) {
    res.status(500).json({ error: error.message });
  }
});

// Speaker & Input gains - api_gains.cpp handlePutSpeakerGain: query params
// speaker + value, 0-100 percent (the ESP stores value/100 internally)
app.put('/gains/speaker', async (req, res) => {
  const { speaker, value } = req.query;

  if (speaker === undefined || value === undefined) {
    return res.status(400).json({ error: 'Missing speaker/value parameters' });
  }

  if (!['left', 'right', 'sub'].includes(speaker)) {
    return res.status(400).json({ error: 'Invalid speaker' });
  }

  // api_gains.cpp handlePutSpeakerGain clamps out-of-range values into
  // 0-100 instead of rejecting them (and toFloat() reads garbage as 0)
  const parsed = parseFloat(value);
  const gain = clamp(isNaN(parsed) ? 0 : parsed, 0, 100);

  try {
    await setSetting(`${speaker}_gain`, gain.toString());
    res.json({ success: true });
  } catch (error) {
    res.status(500).json({ error: error.message });
  }
});

// api_gains.cpp handlePutInputGains: JSON body, every key optional (missing
// keys keep their current value)
app.put('/gains/input', async (req, res) => {
  const body = req.body || {};
  const settingKeys = {
    spdif: 'spdif_gain',
    bluetooth: 'bluetooth_gain',
    usb: 'usb_gain',
    tone: 'tone_gain',
    analog: 'analog_gain'
  };

  try {
    for (const [key, setting] of Object.entries(settingKeys)) {
      if (body[key] !== undefined) {
        await setSetting(setting, body[key].toString());
      }
    }
    res.json({ success: true });
  } catch (error) {
    res.status(500).json({ error: error.message });
  }
});

// Preset gains - api_gains.cpp + config.cpp, 0-100 percent per speaker
app.get('/preset/gains', (req, res) => {
  const presetName = req.query.preset_name;
  if (!presetName) {
    return res.status(400).json({ error: 'Missing preset_name' });
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
  if (!presetName) {
    return res.status(400).json({ error: 'Missing preset_name' });
  }

  // Like the ESP (config_set_preset_gains), only channels present in the
  // body are updated - a partial payload must not zero the omitted ones
  db.get("SELECT gains FROM presets WHERE name = ?", [presetName], (err, row) => {
    if (err) {
      return res.status(500).json({ error: err.message });
    }
    if (!row) {
      return res.status(404).json({ error: 'Preset not found' });
    }

    let gains;
    try { gains = JSON.parse(row.gains); } catch (e) { gains = { left: 100, right: 100, sub: 100 }; }
    for (const channel of ['left', 'right', 'sub']) {
      if (typeof req.body[channel] === 'number' && Number.isFinite(req.body[channel])) {
        gains[channel] = req.body[channel];
      }
    }

    db.run("UPDATE presets SET gains = ? WHERE name = ?", [JSON.stringify(gains), presetName], function(err) {
      if (err) {
        return res.status(500).json({ error: err.message });
      }
      res.json({ success: true });
    });
  });
});

// FIR Filter Management - api_fir.cpp handleGetFirFiles returns a plain
// JSON array of filenames ([] when the Teensy's list is unavailable)
app.get('/fir/files', (req, res) => {
  res.json([
    'fir_flat.txt',
    'fir_room1.txt',
    'fir_room2.txt',
    'fir_speaker1.txt',
    'fir_speaker2.txt'
  ]);
});

app.put('/preset/fir/enabled', (req, res) => {
  const presetName = req.query.preset_name;
  const state = req.query.state;

  if (!presetName) {
    return res.status(400).json({ error: 'Missing preset_name parameter' });
  }
  if (!['on', 'off'].includes(state)) {
    return res.status(400).json({ error: 'Invalid state' });
  }

  const enabled = state === 'on';

  db.run("UPDATE presets SET is_fir_enabled = ? WHERE name = ?", [enabled ? 1 : 0, presetName], function(err) {
    if (err) {
      return res.status(500).json({ error: err.message });
    }

    if (this.changes === 0) {
      return res.status(404).json({ error: 'Preset not found' });
    }

    const payload = {
      messageType: 'firEnabledChanged',
      presetName,
      status: 'ok',
      FIRFiltersEnabled: enabled
    };
    broadcast(payload);
    res.json(payload);
  });
});

app.put('/preset/fir', (req, res) => {
  const presetName = req.query.preset_name;
  const speaker = req.query.speaker;
  const file = req.query.file; // empty string clears the filter

  if (!presetName || !speaker || file === undefined) {
    return res.status(400).json({ error: 'Missing required parameters' });
  }

  if (!['left', 'right', 'sub'].includes(speaker)) {
    return res.status(400).json({ error: 'Invalid speaker' });
  }

  const column = `fir_${speaker}`;
  db.run(`UPDATE presets SET ${column} = ? WHERE name = ?`,
    [file, presetName],
    function(err) {
      if (err) {
        return res.status(500).json({ error: err.message });
      }

      if (this.changes === 0) {
        return res.status(404).json({ error: 'Preset not found' });
      }

      const payload = {
        messageType: 'firChanged',
        presetName,
        status: 'ok',
        speaker,
        filename: file
      };
      broadcast(payload);
      res.json(payload);
    }
  );
});

// Active preset - api_presets.cpp handlePutActivePreset
app.put('/preset/active', (req, res) => {
  const name = req.query.name;

  if (!name) {
    return res.status(400).json({ error: 'Missing required parameters' });
  }

  db.get("SELECT name FROM presets WHERE name = ?", [name], (err, row) => {
    if (err) {
      return res.status(500).json({ error: err.message });
    }
    if (!row) {
      return res.status(404).json({ error: 'Preset not found' });
    }

    db.run("UPDATE presets SET is_current = 0", (err) => {
      if (err) {
        return res.status(500).json({ error: err.message });
      }

      db.run("UPDATE presets SET is_current = 1 WHERE name = ?", [name], (err) => {
        if (err) {
          return res.status(500).json({ error: err.message });
        }

        db.all("SELECT name FROM presets ORDER BY rowid", (err, rows) => {
          const index = rows ? rows.findIndex(r => r.name === name) : 0;
          broadcast({
            messageType: 'activePresetChanged',
            activePresetName: name,
            activePresetIndex: index
          });
          res.json({});
        });
      });
    });
  });
});

// Feature enablement - the ESP takes preset_name + enabled={on|off}
app.put('/preset/delay/enabled', (req, res) => {
  const presetName = req.query.preset_name;
  const state = req.query.enabled;

  if (!presetName) {
    return res.status(400).json({ error: 'Missing preset_name parameter' });
  }
  if (!['on', 'off'].includes(state)) {
    return res.status(400).json({ error: "Invalid state. Must be 'on' or 'off'" });
  }

  const enabled = state === 'on';

  db.run("UPDATE presets SET is_speaker_delay_enabled = ? WHERE name = ?", [enabled ? 1 : 0, presetName], function(err) {
    if (err) {
      return res.status(500).json({ error: err.message });
    }

    if (this.changes === 0) {
      return res.status(404).json({ error: 'Preset not found' });
    }

    const payload = { messageType: 'delayEnabledChanged', presetName, status: 'ok', enabled };
    broadcast(payload);
    res.json(payload);
  });
});

app.put('/preset/eq/enabled', (req, res) => {
  const presetName = req.query.preset_name;
  const state = req.query.enabled;

  if (!presetName) {
    return res.status(400).json({ error: 'Missing preset_name parameter' });
  }
  if (!['on', 'off'].includes(state)) {
    return res.status(400).json({ error: "Invalid state. Must be 'on' or 'off'" });
  }

  const enabled = state === 'on';

  db.run("UPDATE presets SET is_preference_eq_enabled = ? WHERE name = ?", [enabled ? 1 : 0, presetName], function(err) {
    if (err) {
      return res.status(500).json({ error: err.message });
    }

    if (this.changes === 0) {
      return res.status(404).json({ error: 'Preset not found' });
    }

    const payload = { messageType: 'eqEnabledChanged', presetName, status: 'ok', enabled };
    broadcast(payload);
    res.json(payload);
  });
});

app.put('/preset/crossover/enabled', (req, res) => {
  const presetName = req.query.preset_name;
  const state = req.query.enabled;

  if (!presetName) {
    return res.status(400).json({ error: 'Missing preset_name parameter' });
  }
  if (!['on', 'off'].includes(state)) {
    return res.status(400).json({ error: 'Invalid state' });
  }

  const enabled = state === 'on';

  db.run("UPDATE presets SET is_crossover_enabled = ? WHERE name = ?", [enabled ? 1 : 0, presetName], function(err) {
    if (err) {
      return res.status(500).json({ error: err.message });
    }

    if (this.changes === 0) {
      return res.status(404).json({ error: 'Preset not found' });
    }

    const payload = { messageType: 'crossoverEnabledChanged', presetName, status: 'ok', crossoverEnabled: enabled };
    broadcast(payload);
    res.json(payload);
  });
});

// Speaker Configuration - Delay (api_presets.cpp handlePutPresetDelayNamed:
// preset_name + speaker + value, 0-20000 microseconds)
app.put('/preset/delay', (req, res) => {
  const presetName = req.query.preset_name;
  const speaker = req.query.speaker;
  const value = req.query.value;

  if (!presetName || !speaker || value === undefined) {
    return res.status(400).json({ error: 'Missing required parameters' });
  }

  if (!['left', 'right', 'sub'].includes(speaker)) {
    return res.status(400).json({ error: "Invalid speaker. Must be 'left', 'right', or 'sub'" });
  }

  const delayUs = parseFloat(value);
  if (isNaN(delayUs) || delayUs < 0 || delayUs > 20000) {
    return res.status(400).json({ error: 'Delay must be between 0 and 20,000 microseconds' });
  }

  // Update the one speaker atomically in SQL: a read-modify-write in JS
  // loses updates when two delay PUTs arrive concurrently (the UI saves
  // each speaker independently)
  db.run("UPDATE presets SET speaker_delays = json_set(speaker_delays, ?, ?) WHERE name = ?",
    [`$.${speaker}`, delayUs, presetName],
    function(err) {
      if (err) {
        return res.status(500).json({ error: err.message });
      }
      if (this.changes === 0) {
        return res.status(404).json({ error: 'Preset not found' });
      }

      const payload = { messageType: 'delayChanged', presetName, status: 'ok', speaker, delayUs };
      broadcast(payload);
      res.json(payload);
    });
});

// EQ Points (JSON body) - api_preset_config.cpp handlePutPresetEQPoints:
// preset_name query param + array body, values clamped, replies 204
app.put('/preset/eq', (req, res) => {
  const presetName = req.query.preset_name;
  if (!presetName) {
    return res.status(400).json({ error: 'Missing required parameters' });
  }

  const pointsArray = req.body;
  if (!Array.isArray(pointsArray)) {
    return res.status(400).json({ error: 'Expected a JSON array of PEQ points' });
  }
  if (pointsArray.length > MAX_PEQ_POINTS) {
    return res.status(400).json({ error: 'Too many PEQ points' });
  }

  const points = pointsArray.map((point) => ({
    freq: clamp(Number(point.freq ?? 1000), 20, 20000),
    gain: clamp(Number(point.gain ?? 0), -15, 15),
    q: clamp(Number(point.q ?? 1), 0.1, 10)
  }));

  db.get("SELECT name FROM presets WHERE name = ?", [presetName], (err, preset) => {
    if (err) {
      return res.status(500).json({ error: err.message });
    }
    if (!preset) {
      return res.status(404).json({ error: 'Preset not found' });
    }

    db.run(
      "INSERT OR REPLACE INTO eq_configs (preset_name, type, spl, peq_data) VALUES (?, 'pref', 0, ?)",
      [presetName, JSON.stringify(points)],
      function(err) {
        if (err) {
          return res.status(500).json({ error: err.message });
        }

        broadcast({
          messageType: 'eqPointsChanged',
          presetName,
          status: 'ok',
          eqType: 'pref',
          spl: 0,
          numPoints: points.length
        });

        res.status(204).end();
      }
    );
  });
});

// Single EQ point (hot path while dragging) - api_preset_config.cpp
// handlePutPresetEQPoint: rejects out-of-bounds ids and ids that would leave
// a gap (only update-in-place or append), replies 204 without broadcasting
app.put('/preset/eq/point', (req, res) => {
  const presetName = req.query.preset_name;
  if (!presetName) {
    return res.status(400).json({ error: 'Missing required parameters' });
  }

  const point = req.body;
  if (!point || typeof point !== 'object' || Array.isArray(point)) {
    return res.status(400).json({ error: 'Expected a JSON PEQ point object' });
  }

  const id = Number.isInteger(point.id) ? point.id : -1;
  if (id < 0 || id >= MAX_PEQ_POINTS) {
    return res.status(400).json({ error: 'PEQ point ID out of bounds' });
  }

  db.get("SELECT name FROM presets WHERE name = ?", [presetName], (err, preset) => {
    if (err) {
      return res.status(500).json({ error: err.message });
    }
    if (!preset) {
      return res.status(404).json({ error: 'Preset not found' });
    }

    db.get(
      "SELECT peq_data FROM eq_configs WHERE preset_name = ? AND type = 'pref' AND spl = 0",
      [presetName],
      (err, row) => {
        if (err) {
          return res.status(500).json({ error: err.message });
        }

        let points = [];
        if (row) {
          try { points = JSON.parse(row.peq_data); } catch (e) { points = []; }
        }
        if (id > points.length) {
          return res.status(400).json({ error: 'PEQ point ID would leave a gap' });
        }
        points[id] = {
          freq: clamp(Number(point.freq ?? 1000), 20, 20000),
          gain: clamp(Number(point.gain ?? 0), -15, 15),
          q: clamp(Number(point.q ?? 1), 0.1, 10)
        };

        db.run(
          "INSERT OR REPLACE INTO eq_configs (preset_name, type, spl, peq_data) VALUES (?, 'pref', 0, ?)",
          [presetName, JSON.stringify(points)],
          function(err) {
            if (err) {
              return res.status(500).json({ error: err.message });
            }
            res.status(204).end();
          }
        );
      }
    );
  });
});

// Crossover - api_preset_config.cpp handlePutPresetCrossover (20-20000 Hz)
app.put('/preset/crossover', (req, res) => {
  const presetName = req.query.preset_name;
  const freq = req.query.frequency;

  if (!presetName || freq === undefined) {
    return res.status(400).json({ error: 'Missing required parameters' });
  }

  const frequency = parseInt(freq);

  if (isNaN(frequency) || frequency < 20 || frequency > 20000) {
    return res.status(400).json({ error: 'Crossover frequency must be between 20 and 20000 Hz' });
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

      const payload = { messageType: 'crossoverChanged', presetName, status: 'ok', crossoverFreq: frequency };
      broadcast(payload);
      res.json(payload);
    }
  );
});

// Preset Management
app.get('/presets', (req, res) => {
  db.all("SELECT name, is_current FROM presets ORDER BY rowid", (err, rows) => {
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
  const name = req.query.name;

  if (!name) {
    return res.status(400).json({ error: 'Missing required parameters' });
  }

  db.get("SELECT name FROM presets WHERE name = ?", [name], (err, existing) => {
    if (err) {
      return res.status(500).json({ error: err.message });
    }
    if (!existing) {
      return res.status(404).json({ error: 'Preset not found' });
    }

    // api_presets.cpp handleDeletePreset: refuse to delete the last
    // remaining preset (names can be changed, so protecting "Default" by
    // name wouldn't protect anything)
    db.get("SELECT COUNT(*) as count FROM presets", (err, countRow) => {
      if (err) {
        return res.status(500).json({ error: err.message });
      }
      if (countRow.count <= 1) {
        return res.status(400).json({ error: 'Cannot delete the last remaining preset' });
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

          // Like the ESP, deleting the active preset falls back to the
          // first remaining preset
          db.get("SELECT name FROM presets WHERE is_current = 1", (err, row) => {
            if (!err && !row) {
              db.run("UPDATE presets SET is_current = 1 WHERE rowid = (SELECT MIN(rowid) FROM presets)");
            }
            res.json({});
          });
        });
      });
    });
  });
});

// Full preset - shape matches api_presets.cpp handleGetPreset
app.get('/preset', (req, res) => {
  const name = req.query.name;
  if (!name) {
    return res.status(400).json({ error: 'Missing required parameters' });
  }

  db.get("SELECT * FROM presets WHERE name = ?", [name], (err, preset) => {
    if (err) {
      return res.status(500).json({ error: err.message });
    }
    if (!preset) {
      return res.status(404).json({ error: 'Preset not found' });
    }

    db.all("SELECT spl, peq_data FROM eq_configs WHERE preset_name = ? AND type = 'pref' ORDER BY spl", [name], (err, eqRows) => {
      if (err) {
        return res.status(500).json({ error: err.message });
      }

      const preferenceEQ = [];
      eqRows.forEach(row => {
        try {
          const peqData = JSON.parse(row.peq_data);
          preferenceEQ.push({
            spl: row.spl,
            peqs: peqData.map(p => ({ freq: p.freq, gain: p.gain, q: p.q }))
          });
        } catch (e) {
          console.error('Error parsing PEQ data for row:', row, e);
        }
      });

      res.json({
        name: preset.name,
        isCurrent: Boolean(preset.is_current),
        speakerDelays: JSON.parse(preset.speaker_delays),
        isSpeakerDelayEnabled: Boolean(preset.is_speaker_delay_enabled),
        crossoverFreq: parseInt(preset.crossover_freq),
        isCrossoverEnabled: Boolean(preset.is_crossover_enabled),
        isFIREnabled: Boolean(preset.is_fir_enabled),
        firLeft: preset.fir_left,
        firRight: preset.fir_right,
        firSub: preset.fir_sub,
        isPreferenceEQEnabled: Boolean(preset.is_preference_eq_enabled),
        preferenceEQ
      });
    });
  });
});

app.post('/preset', (req, res) => {
  const action = req.query.action;

  if (action === 'create') {
    // api_presets.cpp handlePostPresetCreate: everything starts disabled,
    // crossover at 80 Hz, and a default spl=0 preference set of three flat
    // points at 100/1000/10000 Hz. Does not change the active preset.
    const name = req.query.name;

    if (!name) {
      return res.status(400).json({ error: 'Missing required parameters' });
    }

    const defaultPEQPoints = [
      { freq: 100, gain: 0, q: 1 },
      { freq: 1000, gain: 0, q: 1 },
      { freq: 10000, gain: 0, q: 1 }
    ];

    db.get("SELECT name FROM presets WHERE name = ?", [name], (err, existing) => {
      if (err) {
        return res.status(500).json({ error: err.message });
      }
      if (existing) {
        return res.status(409).json({ error: 'Preset name already exists' });
      }

      db.run(
        `INSERT INTO presets (name, is_current, crossover_freq, speaker_delays,
           is_speaker_delay_enabled, is_crossover_enabled, is_fir_enabled, is_preference_eq_enabled)
         VALUES (?, 0, '80', '{"left": 0, "right": 0, "sub": 0}', 0, 0, 0, 0)`,
        [name],
        function(err) {
          if (err) {
            return res.status(500).json({ error: `Failed to create preset: ${err.message}` });
          }

          db.run(
            "INSERT INTO eq_configs (preset_name, type, spl, peq_data) VALUES (?, 'pref', 0, ?)",
            [name, JSON.stringify(defaultPEQPoints)],
            function(err) {
              if (err) {
                return res.status(500).json({ error: `Failed to add preference curve settings: ${err.message}` });
              }
              res.status(201).json({});
            }
          );
        }
      );
    });
  } else if (action === 'copy') {
    // api_presets.cpp handlePostPresetCopy: source + destination params
    const sourceName = req.query.source;
    const destName = req.query.destination;

    if (!sourceName || !destName) {
      return res.status(400).json({ error: 'Missing required parameters' });
    }

    db.get("SELECT name FROM presets WHERE name = ?", [destName], (err, existing) => {
      if (err) {
        return res.status(500).json({ error: err.message });
      }
      if (existing) {
        return res.status(409).json({ error: 'Destination preset name already exists' });
      }

      db.get("SELECT * FROM presets WHERE name = ?", [sourceName], (err, source) => {
        if (err) {
          return res.status(500).json({ error: err.message });
        }
        if (!source) {
          return res.status(404).json({ error: 'Source preset not found' });
        }

        db.run(`INSERT INTO presets (name, speaker_delays, crossover_freq, is_current, is_speaker_delay_enabled, is_crossover_enabled, is_fir_enabled, is_preference_eq_enabled, fir_left, fir_right, fir_sub, gains)
                VALUES (?, ?, ?, 0, ?, ?, ?, ?, ?, ?, ?, ?)`,
               [destName, source.speaker_delays, source.crossover_freq, source.is_speaker_delay_enabled, source.is_crossover_enabled, source.is_fir_enabled, source.is_preference_eq_enabled, source.fir_left, source.fir_right, source.fir_sub, source.gains],
               function(err) {
          if (err) {
            return res.status(500).json({ error: err.message });
          }

          // Copy EQ configurations
          db.all("SELECT type, spl, peq_data FROM eq_configs WHERE preset_name = ?", [sourceName], (err, eqRows) => {
            if (err) {
              return res.status(500).json({ error: err.message });
            }

            const stmt = db.prepare("INSERT INTO eq_configs (preset_name, type, spl, peq_data) VALUES (?, ?, ?, ?)");
            eqRows.forEach(row => {
              stmt.run(destName, row.type, row.spl, row.peq_data);
            });
            stmt.finalize();

            res.status(201).json({});
          });
        });
      });
    });
  } else {
    res.status(400).json({ error: 'Missing or unknown action' });
  }
});

app.put('/preset', (req, res) => {
  const action = req.query.action;

  if (action === 'rename') {
    // api_presets.cpp handlePutPresetRename: old_name + new_name params
    const oldName = req.query.old_name;
    const newName = req.query.new_name;

    if (!oldName || !newName) {
      return res.status(400).json({ error: 'Missing required parameters' });
    }

    db.run("UPDATE presets SET name = ? WHERE name = ?", [newName, oldName], function(err) {
      if (err) {
        if (err.code === 'SQLITE_CONSTRAINT') {
          return res.status(409).json({ error: 'New preset name already exists' });
        }
        return res.status(500).json({ error: err.message });
      }

      if (this.changes === 0) {
        return res.status(404).json({ error: 'Preset to rename not found' });
      }

      // Update EQ configurations
      db.run("UPDATE eq_configs SET preset_name = ? WHERE preset_name = ?", [newName, oldName], (err) => {
        if (err) {
          return res.status(500).json({ error: err.message });
        }

        res.json({});
      });
    });
  } else {
    res.status(400).json({ error: 'Missing or unknown action' });
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
const httpServer = app.listen(PORT, () => {
  // Report the actual port (PORT=0 asks the OS for an ephemeral one)
  console.log(`Vybes mock server running on port ${httpServer.address().port}`);
  console.log(`Add "127.0.0.1 vybes.local" to your hosts file`);
  console.log(`Database: ${dbPath}`);
});

// Graceful shutdown
process.on('SIGINT', () => {
  console.log('\nShutting down server...');
  db.close();
  process.exit(0);
});
