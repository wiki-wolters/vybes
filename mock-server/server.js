const express = require('express');
const sqlite3 = require('sqlite3').verbose();
const WebSocket = require('ws');
const cors = require('cors');
const path = require('path');
const {
  NUM_OUTPUTS,
  FIR_TAP_POOL,
  MAX_OUTPUT_PEQ,
  MAX_INPUT_PEQ,
  MAX_DELAY_US,
  CROSSOVER_TYPES,
  DEFAULT_TEMPLATE,
  buildPresetConfig,
  listTemplates,
} = require('./templates');

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

// Mirrors MAX_PEQ_POINTS in ESP/esp-web-server/config.h (input EQ)
const MAX_PEQ_POINTS = MAX_INPUT_PEQ;

const clamp = (value, lo, hi) => Math.min(hi, Math.max(lo, value));

// Output gain range in dB
const GAIN_DB_MIN = -40;
const GAIN_DB_MAX = 10;

// Fake tap counts per FIR file. The real ESP will derive these from file
// sizes reported by the Teensy; the mock uses a fixed map (unknown files
// count as 2048 taps).
const FIR_FILE_TAPS = {
  'fir_flat.txt': 1024,
  'fir_room1.txt': 4096,
  'fir_room2.txt': 4096,
  'fir_speaker1.txt': 2048,
  'fir_speaker2.txt': 2048,
};
const firTaps = (file) => (file ? (FIR_FILE_TAPS[file] ?? 2048) : 0);

function firPool(config) {
  const outputs = config.outputs.map((o, i) => ({ output: i, file: o.fir, taps: firTaps(o.fir) }));
  return {
    total: FIR_TAP_POOL,
    used: outputs.reduce((sum, o) => sum + o.taps, 0),
    outputs,
  };
}

// Initialize database tables. Presets store the full V1 config as one JSON
// document (see docs/CHANNEL_ARCHITECTURE.md); a pre-V1 database (columnar
// left/right/sub schema) is dropped and reseeded - mock data is disposable.
db.serialize(() => {
  // System settings table
  db.run(`CREATE TABLE IF NOT EXISTS system_settings (
    key TEXT PRIMARY KEY,
    value TEXT
  )`);

  db.all("PRAGMA table_info(presets)", (err, cols) => {
    const isLegacy = !err && Array.isArray(cols) && cols.length > 0 && !cols.some((c) => c.name === 'config');
    db.serialize(() => {
      if (isLegacy) {
        console.log('Pre-V1 mock database detected: dropping presets and reseeding (mock data is disposable)');
        db.run("DROP TABLE IF EXISTS presets");
        db.run("DROP TABLE IF EXISTS eq_configs");
      }

      db.run(`CREATE TABLE IF NOT EXISTS presets (
        name TEXT PRIMARY KEY,
        is_current INTEGER DEFAULT 0,
        config TEXT NOT NULL,
        created_at DATETIME DEFAULT CURRENT_TIMESTAMP
      )`);

      // Create default preset if none exist
      db.get("SELECT COUNT(*) as count FROM presets", (err, row) => {
        if (!err && row && row.count === 0) {
          db.run("INSERT INTO presets (name, is_current, config) VALUES ('Default', 1, ?)",
            [JSON.stringify(buildPresetConfig(DEFAULT_TEMPLATE))]);
        }
      });
    });
  });

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

// Promise wrappers for the preset handlers (async/await keeps the many V1
// endpoints readable)
const dbGet = (sql, params = []) => new Promise((resolve, reject) => {
  db.get(sql, params, (err, row) => (err ? reject(err) : resolve(row)));
});
const dbAll = (sql, params = []) => new Promise((resolve, reject) => {
  db.all(sql, params, (err, rows) => (err ? reject(err) : resolve(rows)));
});
const dbRun = (sql, params = []) => new Promise((resolve, reject) => {
  db.run(sql, params, function (err) { (err ? reject(err) : resolve(this)); });
});

/** Load a preset row and parse its config. Returns null when missing. */
async function loadPreset(name) {
  const row = await dbGet("SELECT name, is_current, config FROM presets WHERE name = ?", [name]);
  if (!row) return null;
  return { name: row.name, isCurrent: Boolean(row.is_current), config: JSON.parse(row.config) };
}

/** Write a single JSON path inside a preset's config atomically. */
function saveConfigPath(name, jsonPath, value) {
  const isScalar = typeof value === 'number' || typeof value === 'string' || typeof value === 'boolean';
  if (isScalar) {
    // Booleans become json true/false (not 0/1) so parses stay consistent
    if (typeof value === 'boolean') {
      return dbRun("UPDATE presets SET config = json_set(config, ?, json(?)) WHERE name = ?",
        [jsonPath, JSON.stringify(value), name]);
    }
    return dbRun("UPDATE presets SET config = json_set(config, ?, ?) WHERE name = ?",
      [jsonPath, value, name]);
  }
  return dbRun("UPDATE presets SET config = json_set(config, ?, json(?)) WHERE name = ?",
    [jsonPath, JSON.stringify(value), name]);
}

/** Full config write - for structural, multi-field edits only. */
function saveConfig(name, config) {
  return dbRun("UPDATE presets SET config = ? WHERE name = ?", [JSON.stringify(config), name]);
}

/** Async express handler wrapper */
const wrap = (fn) => (req, res) => {
  Promise.resolve(fn(req, res)).catch((err) => {
    console.error(err);
    if (!res.headersSent) res.status(500).json({ error: err.message });
  });
};

/**
 * Common preset + output parameter handling. Replies with the right error
 * and returns null, or returns {preset, outputIndex}.
 */
async function requirePresetOutput(req, res) {
  const presetName = req.query.preset_name;
  if (!presetName) {
    res.status(400).json({ error: 'Missing preset_name parameter' });
    return null;
  }
  const outputParam = req.query.output;
  const outputIndex = Number(outputParam);
  if (outputParam === undefined || !Number.isInteger(outputIndex) || outputIndex < 0 || outputIndex >= NUM_OUTPUTS) {
    res.status(400).json({ error: `Output must be an integer 0-${NUM_OUTPUTS - 1}` });
    return null;
  }
  const preset = await loadPreset(presetName);
  if (!preset) {
    res.status(404).json({ error: 'Preset not found' });
    return null;
  }
  return { preset, outputIndex };
}

/**
 * The effective high-pass frequency of an output: the manual or referenced
 * crossover frequency, or 0 when the HP is off/unresolvable.
 */
function effectiveHpFreq(output, crossovers) {
  const hp = output.hp || { mode: 'off' };
  if (hp.mode === 'manual') return Number(hp.freq) || 0;
  if (hp.mode === 'xover') {
    const point = crossovers.find((c) => c.id === hp.xover);
    return point ? point.freq : 0;
  }
  return 0;
}

/**
 * Safety check: would this config leave any output's effective HP below its
 * hpFloor? Returns an error string or null. This is the driver-protection
 * backstop - it must hold no matter which endpoint made the edit.
 */
function hpFloorViolation(config) {
  for (let i = 0; i < config.outputs.length; i++) {
    const output = config.outputs[i];
    if (!output.enabled || !(output.hpFloor > 0)) continue;
    const freq = effectiveHpFreq(output, config.crossovers);
    if (freq < output.hpFloor) {
      return `Output ${i + 1} (${output.label}) requires a high-pass at or above ${output.hpFloor} Hz`;
    }
  }
  return null;
}

const parseOnOff = (value) => (value === 'on' ? true : value === 'off' ? false : null);

// Broadcast helper for single-output edits: carries the changed fields so
// the UI store can merge without refetching.
function broadcastOutputChanged(presetName, outputIndex, changes, extra = {}) {
  const payload = {
    messageType: 'outputChanged',
    presetName,
    status: 'ok',
    output: outputIndex,
    changes,
    ...extra,
  };
  broadcast(payload);
  return payload;
}

/**
 * Structural edits (routing, crossover sections, enabling outputs) take a
 * preset beyond what its template's simple view can express: flip it to
 * "custom" so the UI renders the full editor. Cosmetic/tuning edits (gain,
 * delay, PEQ, FIR, labels, mute) keep the template. Returns the extra
 * fields for the outputChanged payload.
 */
async function flipTemplateToCustom(preset) {
  if (preset.config.template === 'custom') return {};
  await saveConfigPath(preset.name, '$.template', 'custom');
  return { template: 'custom' };
}

// ===== API ROUTES =====
// The V1 preset/output/crossover endpoints below ARE the contract the new
// ESP32-S3 firmware implements (docs/CHANNEL_ARCHITECTURE.md). System-level
// routes still mirror the existing ESP handlers (api_*.cpp).

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

// ===== Templates =====

app.get('/templates', (req, res) => {
  res.json(listTemplates());
});

// ===== Preset listing / full preset =====

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

// Full preset - the V1 shape
app.get('/preset', wrap(async (req, res) => {
  const name = req.query.name;
  if (!name) {
    return res.status(400).json({ error: 'Missing required parameters' });
  }

  const preset = await loadPreset(name);
  if (!preset) {
    return res.status(404).json({ error: 'Preset not found' });
  }

  const c = preset.config;
  const pool = firPool(c);
  res.json({
    name: preset.name,
    isCurrent: preset.isCurrent,
    template: c.template,
    crossovers: c.crossovers,
    inputEq: c.inputEq,
    outputs: c.outputs,
    delaysEnabled: c.delaysEnabled,
    firEnabled: c.firEnabled,
    firPool: { total: pool.total, used: pool.used },
  });
}));

// ===== Preset CRUD =====

app.post('/preset', wrap(async (req, res) => {
  const action = req.query.action;

  if (action === 'create') {
    // Creates a preset from a template (default 2.1). Everything starts
    // disabled. Does not change the active preset.
    const name = req.query.name;
    const templateId = req.query.template || DEFAULT_TEMPLATE;

    if (!name) {
      return res.status(400).json({ error: 'Missing required parameters' });
    }

    let config;
    try {
      config = buildPresetConfig(templateId);
    } catch (e) {
      return res.status(400).json({ error: `Unknown template: ${templateId}` });
    }

    const existing = await dbGet("SELECT name FROM presets WHERE name = ?", [name]);
    if (existing) {
      return res.status(409).json({ error: 'Preset name already exists' });
    }

    await dbRun("INSERT INTO presets (name, is_current, config) VALUES (?, 0, ?)",
      [name, JSON.stringify(config)]);
    res.status(201).json({});
  } else if (action === 'copy') {
    const sourceName = req.query.source;
    const destName = req.query.destination;

    if (!sourceName || !destName) {
      return res.status(400).json({ error: 'Missing required parameters' });
    }

    const existing = await dbGet("SELECT name FROM presets WHERE name = ?", [destName]);
    if (existing) {
      return res.status(409).json({ error: 'Destination preset name already exists' });
    }

    const source = await dbGet("SELECT config FROM presets WHERE name = ?", [sourceName]);
    if (!source) {
      return res.status(404).json({ error: 'Source preset not found' });
    }

    await dbRun("INSERT INTO presets (name, is_current, config) VALUES (?, 0, ?)",
      [destName, source.config]);
    res.status(201).json({});
  } else {
    res.status(400).json({ error: 'Missing or unknown action' });
  }
}));

app.put('/preset', wrap(async (req, res) => {
  const action = req.query.action;

  if (action === 'rename') {
    const oldName = req.query.old_name;
    const newName = req.query.new_name;

    if (!oldName || !newName) {
      return res.status(400).json({ error: 'Missing required parameters' });
    }

    try {
      const result = await dbRun("UPDATE presets SET name = ? WHERE name = ?", [newName, oldName]);
      if (result.changes === 0) {
        return res.status(404).json({ error: 'Preset to rename not found' });
      }
      res.json({});
    } catch (err) {
      if (err.code === 'SQLITE_CONSTRAINT') {
        return res.status(409).json({ error: 'New preset name already exists' });
      }
      throw err;
    }
  } else {
    res.status(400).json({ error: 'Missing or unknown action' });
  }
}));

app.delete('/preset', wrap(async (req, res) => {
  const name = req.query.name;

  if (!name) {
    return res.status(400).json({ error: 'Missing required parameters' });
  }

  const existing = await dbGet("SELECT name FROM presets WHERE name = ?", [name]);
  if (!existing) {
    return res.status(404).json({ error: 'Preset not found' });
  }

  // api_presets.cpp handleDeletePreset: refuse to delete the last
  // remaining preset (names can be changed, so protecting "Default" by
  // name wouldn't protect anything)
  const countRow = await dbGet("SELECT COUNT(*) as count FROM presets");
  if (countRow.count <= 1) {
    return res.status(400).json({ error: 'Cannot delete the last remaining preset' });
  }

  await dbRun("DELETE FROM presets WHERE name = ?", [name]);

  // Like the ESP, deleting the active preset falls back to the first
  // remaining preset
  const current = await dbGet("SELECT name FROM presets WHERE is_current = 1");
  if (!current) {
    await dbRun("UPDATE presets SET is_current = 1 WHERE rowid = (SELECT MIN(rowid) FROM presets)");
  }
  res.json({});
}));

// Active preset - api_presets.cpp handlePutActivePreset
app.put('/preset/active', wrap(async (req, res) => {
  const name = req.query.name;

  if (!name) {
    return res.status(400).json({ error: 'Missing required parameters' });
  }

  const row = await dbGet("SELECT name FROM presets WHERE name = ?", [name]);
  if (!row) {
    return res.status(404).json({ error: 'Preset not found' });
  }

  await dbRun("UPDATE presets SET is_current = 0");
  await dbRun("UPDATE presets SET is_current = 1 WHERE name = ?", [name]);

  const rows = await dbAll("SELECT name FROM presets ORDER BY rowid");
  const index = rows ? rows.findIndex(r => r.name === name) : 0;
  broadcast({
    messageType: 'activePresetChanged',
    activePresetName: name,
    activePresetIndex: index
  });
  res.json({});
}));

// ===== Crossover points =====

// Set a crossover point's frequency. Points are shared: every output filter
// referencing the id follows. Safety semantics:
//  - locked points reject writes without confirm=true (409)
//  - a change may never leave an output's HP below its hpFloor (409)
app.put('/preset/crossover', wrap(async (req, res) => {
  const presetName = req.query.preset_name;
  const freqStr = req.query.frequency;
  const id = req.query.id;
  const confirmed = req.query.confirm === 'true';

  if (!presetName || freqStr === undefined || !id) {
    return res.status(400).json({ error: 'Missing required parameters' });
  }

  const preset = await loadPreset(presetName);
  if (!preset) {
    return res.status(404).json({ error: 'Preset not found' });
  }

  const config = preset.config;
  const index = config.crossovers.findIndex((x) => x.id === id);
  if (index === -1) {
    return res.status(404).json({ error: `Crossover point not found: ${id}` });
  }
  const point = config.crossovers[index];

  const freq = parseInt(freqStr);
  if (isNaN(freq) || freq < point.min || freq > point.max) {
    return res.status(400).json({ error: `Crossover frequency must be between ${point.min} and ${point.max} Hz` });
  }

  if (point.locked && !confirmed) {
    return res.status(409).json({
      error: `Crossover point ${id} is locked. Re-send with confirm=true to apply.`,
      locked: true,
    });
  }

  const candidate = JSON.parse(JSON.stringify(config));
  candidate.crossovers[index].freq = freq;
  const violation = hpFloorViolation(candidate);
  if (violation) {
    return res.status(409).json({ error: violation });
  }

  await saveConfigPath(presetName, `$.crossovers[${index}].freq`, freq);

  const payload = { messageType: 'crossoverChanged', presetName, status: 'ok', id, crossoverFreq: freq };
  broadcast(payload);
  res.json(payload);
}));

// Bypass/enable a crossover point: toggles mode between 'xover' and 'off'
// on every filter that references it (the xover ref is kept so re-enabling
// restores it). Locked points need confirm=true; hpFloor blocks bypassing
// a protective HP entirely.
app.put('/preset/crossover/enabled', wrap(async (req, res) => {
  const presetName = req.query.preset_name;
  const state = req.query.enabled;
  const id = req.query.id;
  const confirmed = req.query.confirm === 'true';

  if (!presetName || !id) {
    return res.status(400).json({ error: 'Missing required parameters' });
  }
  const enabled = parseOnOff(state);
  if (enabled === null) {
    return res.status(400).json({ error: 'Invalid state' });
  }

  const preset = await loadPreset(presetName);
  if (!preset) {
    return res.status(404).json({ error: 'Preset not found' });
  }

  const config = preset.config;
  const point = config.crossovers.find((x) => x.id === id);
  if (!point) {
    return res.status(404).json({ error: `Crossover point not found: ${id}` });
  }

  if (point.locked && !confirmed) {
    return res.status(409).json({
      error: `Crossover point ${id} is locked. Re-send with confirm=true to apply.`,
      locked: true,
    });
  }

  const candidate = JSON.parse(JSON.stringify(config));
  for (const output of candidate.outputs) {
    for (const which of ['hp', 'lp']) {
      const filter = output[which];
      if (filter.xover === id && ['xover', 'off'].includes(filter.mode)) {
        filter.mode = enabled ? 'xover' : 'off';
      }
    }
  }
  const violation = hpFloorViolation(candidate);
  if (violation) {
    return res.status(409).json({ error: violation });
  }

  await saveConfig(presetName, candidate);

  const payload = { messageType: 'crossoverEnabledChanged', presetName, status: 'ok', id, crossoverEnabled: enabled };
  broadcast(payload);
  res.json(payload);
}));

// ===== Output channels =====

app.put('/preset/output/label', wrap(async (req, res) => {
  const ctx = await requirePresetOutput(req, res);
  if (!ctx) return;
  const label = req.query.label;
  if (!label || label.length > 24) {
    return res.status(400).json({ error: 'Label must be 1-24 characters' });
  }
  await saveConfigPath(ctx.preset.name, `$.outputs[${ctx.outputIndex}].label`, label);
  res.json(broadcastOutputChanged(ctx.preset.name, ctx.outputIndex, { label }));
}));

app.put('/preset/output/enabled', wrap(async (req, res) => {
  const ctx = await requirePresetOutput(req, res);
  if (!ctx) return;
  const enabled = parseOnOff(req.query.state);
  if (enabled === null) {
    return res.status(400).json({ error: 'Invalid state' });
  }
  await saveConfigPath(ctx.preset.name, `$.outputs[${ctx.outputIndex}].enabled`, enabled);
  const extra = await flipTemplateToCustom(ctx.preset);
  res.json(broadcastOutputChanged(ctx.preset.name, ctx.outputIndex, { enabled }, extra));
}));

// Source mix: JSON body {left, right}, each clamped to 0..1
app.put('/preset/output/source', wrap(async (req, res) => {
  const ctx = await requirePresetOutput(req, res);
  if (!ctx) return;
  const body = req.body;
  if (!body || typeof body !== 'object' || Array.isArray(body)
      || typeof body.left !== 'number' || typeof body.right !== 'number') {
    return res.status(400).json({ error: 'Expected a JSON body with numeric left and right' });
  }
  const source = { left: clamp(body.left, 0, 1), right: clamp(body.right, 0, 1) };
  await saveConfigPath(ctx.preset.name, `$.outputs[${ctx.outputIndex}].source`, source);
  const extra = await flipTemplateToCustom(ctx.preset);
  res.json(broadcastOutputChanged(ctx.preset.name, ctx.outputIndex, { source }, extra));
}));

app.put('/preset/output/gain', wrap(async (req, res) => {
  const ctx = await requirePresetOutput(req, res);
  if (!ctx) return;
  const parsed = parseFloat(req.query.value);
  if (isNaN(parsed)) {
    return res.status(400).json({ error: 'Missing or invalid value' });
  }
  const gainDb = clamp(parsed, GAIN_DB_MIN, GAIN_DB_MAX);
  await saveConfigPath(ctx.preset.name, `$.outputs[${ctx.outputIndex}].gainDb`, gainDb);
  res.json(broadcastOutputChanged(ctx.preset.name, ctx.outputIndex, { gainDb }));
}));

app.put('/preset/output/mute', wrap(async (req, res) => {
  const ctx = await requirePresetOutput(req, res);
  if (!ctx) return;
  const mute = parseOnOff(req.query.state);
  if (mute === null) {
    return res.status(400).json({ error: 'Invalid state' });
  }
  await saveConfigPath(ctx.preset.name, `$.outputs[${ctx.outputIndex}].mute`, mute);
  res.json(broadcastOutputChanged(ctx.preset.name, ctx.outputIndex, { mute }));
}));

app.put('/preset/output/invert', wrap(async (req, res) => {
  const ctx = await requirePresetOutput(req, res);
  if (!ctx) return;
  const invert = parseOnOff(req.query.state);
  if (invert === null) {
    return res.status(400).json({ error: 'Invalid state' });
  }
  await saveConfigPath(ctx.preset.name, `$.outputs[${ctx.outputIndex}].invert`, invert);
  res.json(broadcastOutputChanged(ctx.preset.name, ctx.outputIndex, { invert }));
}));

app.put('/preset/output/delay', wrap(async (req, res) => {
  const ctx = await requirePresetOutput(req, res);
  if (!ctx) return;
  const delayUs = parseFloat(req.query.value);
  if (isNaN(delayUs) || delayUs < 0 || delayUs > MAX_DELAY_US) {
    return res.status(400).json({ error: `Delay must be between 0 and ${MAX_DELAY_US} microseconds` });
  }
  await saveConfigPath(ctx.preset.name, `$.outputs[${ctx.outputIndex}].delayUs`, delayUs);
  res.json(broadcastOutputChanged(ctx.preset.name, ctx.outputIndex, { delayUs }));
}));

// HP/LP filter section: which=hp|lp, JSON body
//   {mode: 'off'} | {mode: 'xover', xover: id} | {mode: 'manual', freq, type}
// hpFloor is enforced on HP edits (including switching it off).
app.put('/preset/output/filter', wrap(async (req, res) => {
  const ctx = await requirePresetOutput(req, res);
  if (!ctx) return;
  const which = req.query.which;
  if (!['hp', 'lp'].includes(which)) {
    return res.status(400).json({ error: "Parameter 'which' must be 'hp' or 'lp'" });
  }

  const body = req.body;
  if (!body || typeof body !== 'object' || Array.isArray(body)) {
    return res.status(400).json({ error: 'Expected a JSON filter object' });
  }

  const config = ctx.preset.config;
  let filter;
  if (body.mode === 'off') {
    // Keep a previously referenced crossover id so re-enabling restores it
    const previous = config.outputs[ctx.outputIndex][which];
    filter = previous.xover ? { mode: 'off', xover: previous.xover } : { mode: 'off' };
  } else if (body.mode === 'xover') {
    if (!config.crossovers.some((x) => x.id === body.xover)) {
      return res.status(400).json({ error: `Unknown crossover point: ${body.xover}` });
    }
    filter = { mode: 'xover', xover: body.xover };
  } else if (body.mode === 'manual') {
    const freq = Number(body.freq);
    const type = body.type || 'LR4';
    if (!Number.isFinite(freq) || freq < 20 || freq > 20000) {
      return res.status(400).json({ error: 'Manual filter frequency must be between 20 and 20000 Hz' });
    }
    if (!CROSSOVER_TYPES.includes(type)) {
      return res.status(400).json({ error: `Filter type must be one of ${CROSSOVER_TYPES.join(', ')}` });
    }
    filter = { mode: 'manual', freq, type };
  } else {
    return res.status(400).json({ error: "Filter mode must be 'off', 'xover' or 'manual'" });
  }

  const candidate = JSON.parse(JSON.stringify(config));
  candidate.outputs[ctx.outputIndex][which] = filter;
  const violation = hpFloorViolation(candidate);
  if (violation) {
    return res.status(409).json({ error: violation });
  }

  await saveConfigPath(ctx.preset.name, `$.outputs[${ctx.outputIndex}].${which}`, filter);
  const extra = await flipTemplateToCustom(ctx.preset);
  res.json(broadcastOutputChanged(ctx.preset.name, ctx.outputIndex, { [which]: filter }, extra));
}));

// Output PEQ: array body (<= MAX_OUTPUT_PEQ points), clamped like input EQ
app.put('/preset/output/eq', wrap(async (req, res) => {
  const ctx = await requirePresetOutput(req, res);
  if (!ctx) return;
  const pointsArray = req.body;
  if (!Array.isArray(pointsArray)) {
    return res.status(400).json({ error: 'Expected a JSON array of PEQ points' });
  }
  if (pointsArray.length > MAX_OUTPUT_PEQ) {
    return res.status(400).json({ error: 'Too many PEQ points' });
  }

  const points = pointsArray.map((point) => ({
    freq: clamp(Number(point.freq ?? 1000), 20, 20000),
    gain: clamp(Number(point.gain ?? 0), -15, 15),
    q: clamp(Number(point.q ?? 1), 0.1, 10)
  }));

  await saveConfigPath(ctx.preset.name, `$.outputs[${ctx.outputIndex}].peq`, points);

  broadcast({
    messageType: 'outputEqChanged',
    presetName: ctx.preset.name,
    status: 'ok',
    output: ctx.outputIndex,
    numPoints: points.length,
  });
  res.status(204).end();
}));

// Single output PEQ point (hot path while dragging): update in place or
// append directly after the last point; a gap-leaving id is rejected.
app.put('/preset/output/eq/point', wrap(async (req, res) => {
  const ctx = await requirePresetOutput(req, res);
  if (!ctx) return;
  const point = req.body;
  if (!point || typeof point !== 'object' || Array.isArray(point)) {
    return res.status(400).json({ error: 'Expected a JSON PEQ point object' });
  }

  const id = Number.isInteger(point.id) ? point.id : -1;
  if (id < 0 || id >= MAX_OUTPUT_PEQ) {
    return res.status(400).json({ error: 'PEQ point ID out of bounds' });
  }

  const peq = ctx.preset.config.outputs[ctx.outputIndex].peq;
  if (id > peq.length) {
    return res.status(400).json({ error: 'PEQ point ID would leave a gap' });
  }

  const stored = {
    freq: clamp(Number(point.freq ?? 1000), 20, 20000),
    gain: clamp(Number(point.gain ?? 0), -15, 15),
    q: clamp(Number(point.q ?? 1), 0.1, 10)
  };
  await saveConfigPath(ctx.preset.name, `$.outputs[${ctx.outputIndex}].peq[${id}]`, stored);
  res.status(204).end();
}));

// FIR file per output. The shared tap pool is enforced here: a load that
// would exceed it is rejected with 409 (the UI surfaces used/total).
app.put('/preset/output/fir', wrap(async (req, res) => {
  const ctx = await requirePresetOutput(req, res);
  if (!ctx) return;
  const file = req.query.file;
  if (file === undefined) {
    return res.status(400).json({ error: 'Missing file parameter' });
  }

  const candidate = JSON.parse(JSON.stringify(ctx.preset.config));
  candidate.outputs[ctx.outputIndex].fir = file;
  const pool = firPool(candidate);
  if (pool.used > pool.total) {
    return res.status(409).json({
      error: `FIR tap pool exceeded: ${pool.used} of ${pool.total} taps`,
      used: pool.used,
      total: pool.total,
    });
  }

  await saveConfigPath(ctx.preset.name, `$.outputs[${ctx.outputIndex}].fir`, file);
  res.json(broadcastOutputChanged(ctx.preset.name, ctx.outputIndex, { fir: file }, {
    firPool: { total: pool.total, used: pool.used },
  }));
}));

// Tap pool status for a preset
app.get('/preset/fir/pool', wrap(async (req, res) => {
  const presetName = req.query.preset_name;
  if (!presetName) {
    return res.status(400).json({ error: 'Missing preset_name parameter' });
  }
  const preset = await loadPreset(presetName);
  if (!preset) {
    return res.status(404).json({ error: 'Preset not found' });
  }
  res.json(firPool(preset.config));
}));

// ===== Master toggles (delays / FIR) =====

app.put('/preset/delay/enabled', wrap(async (req, res) => {
  const presetName = req.query.preset_name;
  const enabled = parseOnOff(req.query.enabled);

  if (!presetName) {
    return res.status(400).json({ error: 'Missing preset_name parameter' });
  }
  if (enabled === null) {
    return res.status(400).json({ error: "Invalid state. Must be 'on' or 'off'" });
  }

  const preset = await loadPreset(presetName);
  if (!preset) {
    return res.status(404).json({ error: 'Preset not found' });
  }

  await saveConfigPath(presetName, '$.delaysEnabled', enabled);
  const payload = { messageType: 'delayEnabledChanged', presetName, status: 'ok', enabled };
  broadcast(payload);
  res.json(payload);
}));

app.put('/preset/fir/enabled', wrap(async (req, res) => {
  const presetName = req.query.preset_name;
  const enabled = parseOnOff(req.query.state);

  if (!presetName) {
    return res.status(400).json({ error: 'Missing preset_name parameter' });
  }
  if (enabled === null) {
    return res.status(400).json({ error: 'Invalid state' });
  }

  const preset = await loadPreset(presetName);
  if (!preset) {
    return res.status(404).json({ error: 'Preset not found' });
  }

  await saveConfigPath(presetName, '$.firEnabled', enabled);
  const payload = { messageType: 'firEnabledChanged', presetName, status: 'ok', FIRFiltersEnabled: enabled };
  broadcast(payload);
  res.json(payload);
}));

// ===== Input EQ (shared L/R bus: preference curve + SPL sets) =====

/** Find the spl=0 set index, creating the set if needed. */
function getOrCreateSpl0SetIndex(config) {
  let index = config.inputEq.sets.findIndex((s) => s.spl === 0);
  if (index === -1) {
    config.inputEq.sets.push({ spl: 0, points: [] });
    index = config.inputEq.sets.length - 1;
  }
  return index;
}

// EQ Points (JSON body): array of points, values clamped, replies 204
app.put('/preset/eq', wrap(async (req, res) => {
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

  const preset = await loadPreset(presetName);
  if (!preset) {
    return res.status(404).json({ error: 'Preset not found' });
  }

  const points = pointsArray.map((point) => ({
    freq: clamp(Number(point.freq ?? 1000), 20, 20000),
    gain: clamp(Number(point.gain ?? 0), -15, 15),
    q: clamp(Number(point.q ?? 1), 0.1, 10)
  }));

  const setIndex = getOrCreateSpl0SetIndex(preset.config);
  preset.config.inputEq.sets[setIndex].points = points;
  await saveConfigPath(presetName, '$.inputEq.sets', preset.config.inputEq.sets);

  broadcast({
    messageType: 'eqPointsChanged',
    presetName,
    status: 'ok',
    eqType: 'pref',
    spl: 0,
    numPoints: points.length
  });

  res.status(204).end();
}));

// Single EQ point (hot path while dragging): rejects out-of-bounds ids and
// ids that would leave a gap (only update-in-place or append), replies 204
// without broadcasting
app.put('/preset/eq/point', wrap(async (req, res) => {
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

  const preset = await loadPreset(presetName);
  if (!preset) {
    return res.status(404).json({ error: 'Preset not found' });
  }

  const setIndex = getOrCreateSpl0SetIndex(preset.config);
  const points = preset.config.inputEq.sets[setIndex].points;
  if (id > points.length) {
    return res.status(400).json({ error: 'PEQ point ID would leave a gap' });
  }

  points[id] = {
    freq: clamp(Number(point.freq ?? 1000), 20, 20000),
    gain: clamp(Number(point.gain ?? 0), -15, 15),
    q: clamp(Number(point.q ?? 1), 0.1, 10)
  };
  await saveConfigPath(presetName, `$.inputEq.sets[${setIndex}].points[${id}]`, points[id]);
  res.status(204).end();
}));

app.put('/preset/eq/enabled', wrap(async (req, res) => {
  const presetName = req.query.preset_name;
  const enabled = parseOnOff(req.query.enabled);

  if (!presetName) {
    return res.status(400).json({ error: 'Missing preset_name parameter' });
  }
  if (enabled === null) {
    return res.status(400).json({ error: "Invalid state. Must be 'on' or 'off'" });
  }

  const preset = await loadPreset(presetName);
  if (!preset) {
    return res.status(404).json({ error: 'Preset not found' });
  }

  await saveConfigPath(presetName, '$.inputEq.enabled', enabled);
  const payload = { messageType: 'eqEnabledChanged', presetName, status: 'ok', enabled };
  broadcast(payload);
  res.json(payload);
}));

// ===== FIR file listing =====
// Plain array of filenames, like the Teensy's getFiles reply relayed by the
// ESP ([] when the list is unavailable)
app.get('/fir/files', (req, res) => {
  res.json(Object.keys(FIR_FILE_TAPS));
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

      const backup = {
        settings,
        presets
      };

      res.json(backup);
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
