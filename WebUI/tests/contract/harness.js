/*
 * Test harness for the Vybes API contract suite.
 *
 * Two modes:
 *  - Hermetic (default): spawns mock-server/server.js as a child process on
 *    OS-assigned ephemeral ports with a throwaway sqlite DB in a temp dir,
 *    so runs are isolated and parallel-safe.
 *  - Real device: set VYBES_API_URL (e.g. VYBES_API_URL=http://vybes.local)
 *    and the suite targets it instead. The websocket is derived from the
 *    same origin at /live-updates, exactly like the production UI.
 */
import { spawn } from 'node:child_process'
import { mkdtempSync, rmSync } from 'node:fs'
import { tmpdir } from 'node:os'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
import WebSocket from 'ws'

const __dirname = path.dirname(fileURLToPath(import.meta.url))
const MOCK_SERVER_PATH = path.resolve(__dirname, '../../../mock-server/server.js')

export const REAL_DEVICE = Boolean(process.env.VYBES_API_URL)

/**
 * Start (or attach to) the test target.
 * @returns {Promise<{baseUrl: string, wsUrl: string, isMock: boolean, stop: () => Promise<void>}>}
 */
export async function startTarget() {
  if (REAL_DEVICE) {
    const baseUrl = process.env.VYBES_API_URL.replace(/\/+$/, '')
    const url = new URL(baseUrl)
    const wsProtocol = url.protocol === 'https:' ? 'wss:' : 'ws:'
    return {
      baseUrl,
      wsUrl: `${wsProtocol}//${url.host}/live-updates`,
      isMock: false,
      stop: async () => {},
    }
  }

  const tempDir = mkdtempSync(path.join(tmpdir(), 'vybes-contract-'))
  const child = spawn(process.execPath, [MOCK_SERVER_PATH], {
    env: {
      ...process.env,
      PORT: '0',
      WS_PORT: '0',
      VYBES_DB_PATH: path.join(tempDir, 'vybes.db'),
      NODE_ENV: 'test',
    },
    stdio: ['ignore', 'pipe', 'pipe'],
  })

  let stdout = ''
  let stderr = ''
  child.stdout.on('data', (chunk) => { stdout += chunk })
  child.stderr.on('data', (chunk) => { stderr += chunk })

  // Wait for both listeners to report their OS-assigned ports
  const ports = await new Promise((resolve, reject) => {
    const deadline = setTimeout(() => {
      reject(new Error(`Mock server did not start in time.\nstdout: ${stdout}\nstderr: ${stderr}`))
    }, 20000)
    const check = () => {
      const http = stdout.match(/Vybes mock server running on port (\d+)/)
      const ws = stdout.match(/WebSocket server running on port (\d+)/)
      if (http && ws) {
        clearTimeout(deadline)
        resolve({ http: Number(http[1]), ws: Number(ws[1]) })
      }
    }
    child.stdout.on('data', check)
    child.on('exit', (code) => {
      clearTimeout(deadline)
      reject(new Error(`Mock server exited early (code ${code}).\nstdout: ${stdout}\nstderr: ${stderr}`))
    })
    check()
  })

  const baseUrl = `http://127.0.0.1:${ports.http}`

  // The DB seeds asynchronously on first boot; wait until the default
  // preset exists before handing the target to the tests.
  const seedDeadline = Date.now() + 15000
  for (;;) {
    try {
      const res = await fetch(`${baseUrl}/presets`)
      if (res.ok) {
        const presets = await res.json()
        if (Array.isArray(presets) && presets.length > 0) break
      }
    } catch (e) { /* not up yet */ }
    if (Date.now() > seedDeadline) {
      throw new Error(`Mock server never seeded its default preset.\nstdout: ${stdout}\nstderr: ${stderr}`)
    }
    await new Promise((r) => setTimeout(r, 50))
  }

  return {
    baseUrl,
    wsUrl: `ws://127.0.0.1:${ports.ws}`,
    isMock: true,
    stop: async () => {
      await new Promise((resolve) => {
        child.once('exit', resolve)
        child.kill('SIGTERM')
        setTimeout(() => { child.kill('SIGKILL'); resolve() }, 3000).unref()
      })
      rmSync(tempDir, { recursive: true, force: true })
    },
  }
}

/**
 * Minimal HTTP helper that never throws on non-2xx, so tests can assert
 * status codes. Does not assume error bodies are JSON: the ESP replies
 * text/plain for most errors while the mock replies JSON.
 */
export async function api(baseUrl, method, endpoint, body = undefined) {
  const config = { method, headers: {} }
  if (body !== undefined) {
    config.headers['Content-Type'] = 'application/json'
    config.body = JSON.stringify(body)
  }
  const res = await fetch(baseUrl + endpoint, config)
  const text = await res.text()
  let json = null
  try { json = text ? JSON.parse(text) : null } catch (e) { /* not JSON */ }
  return { status: res.status, ok: res.ok, json, text }
}

/**
 * Websocket probe. `expect(predicate)` must be called BEFORE the HTTP
 * mutation is issued: it only matches messages that arrive after
 * registration, so a fast broadcast can't be missed or double-counted.
 */
export function connectWs(wsUrl) {
  return new Promise((resolve, reject) => {
    const socket = new WebSocket(wsUrl)
    const waiters = []
    const timer = setTimeout(() => reject(new Error(`Websocket connect timeout: ${wsUrl}`)), 10000)

    socket.on('message', (data) => {
      let msg
      try { msg = JSON.parse(data.toString()) } catch (e) { return }
      for (let i = waiters.length - 1; i >= 0; i--) {
        if (waiters[i].predicate(msg)) {
          const [waiter] = waiters.splice(i, 1)
          clearTimeout(waiter.timer)
          waiter.resolve(msg)
        }
      }
    })

    socket.on('error', (err) => { clearTimeout(timer); reject(err) })

    socket.on('open', () => {
      clearTimeout(timer)
      resolve({
        socket,
        expect(predicate, timeoutMs = 10000) {
          return new Promise((res, rej) => {
            const waiter = {
              predicate,
              resolve: res,
              timer: setTimeout(() => {
                const idx = waiters.indexOf(waiter)
                if (idx >= 0) waiters.splice(idx, 1)
                rej(new Error('Timed out waiting for websocket broadcast'))
              }, timeoutMs),
            }
            waiters.push(waiter)
          })
        },
        close() {
          socket.close()
        },
      })
    })
  })
}
