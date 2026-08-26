// The request queue is what keeps the UI inside the device's socket budget.
// The HTTPS listener affords 2 TLS sockets at ~50KB of internal heap each, and
// the live-updates websocket permanently holds one of them, so exactly one is
// left for REST. Two concurrent requests make the browser open a second
// connection, the listener evicts the least-recently-used one, and the evicted
// connection's in-flight request fails - which is what showed up as sporadic
// failed API calls whenever the websocket was connected.
//
// These pin the three properties that make the queue trustworthy: one request
// in flight, in order, and a failure that does not stall everything behind it.
// The 15s abort timeout is not covered here (it needs a fetch mock that
// honours AbortSignal).

import { describe, it, expect, beforeEach, afterEach, vi } from 'vitest'
import { VybesAPI } from '../../src/api-client.js'

const flush = () => new Promise((resolve) => setTimeout(resolve, 0))

function deferred() {
  let resolve
  let reject
  const promise = new Promise((res, rej) => {
    resolve = res
    reject = rej
  })
  return { promise, resolve, reject }
}

function okResponse(bodyText = '{}') {
  return { ok: true, status: 200, statusText: 'OK', text: async () => bodyText }
}

describe('api client request queue', () => {
  let api
  let inFlight
  let maxInFlight
  let paths
  let gates

  beforeEach(() => {
    api = new VybesAPI()
    inFlight = 0
    maxInFlight = 0
    paths = []
    gates = []
    // Each call parks on its own gate, so the test decides when a request
    // finishes and can observe whether a second one started meanwhile.
    global.fetch = vi.fn((url) => {
      inFlight += 1
      maxInFlight = Math.max(maxInFlight, inFlight)
      paths.push(String(url).replace(api.baseUrl, ''))
      const gate = deferred()
      gates.push(gate)
      return gate.promise.finally(() => {
        inFlight -= 1
      })
    })
    // request() logs failures; the rejection tests below expect them.
    vi.spyOn(console, 'error').mockImplementation(() => {})
  })

  afterEach(() => {
    vi.restoreAllMocks()
    delete global.fetch
  })

  it('keeps one request in flight and preserves order', async () => {
    const calls = [
      api.request('GET', '/status'),
      api.request('GET', '/presets'),
      api.request('GET', '/recorder'),
    ]

    await flush()
    expect(global.fetch).toHaveBeenCalledTimes(1)

    gates[0].resolve(okResponse())
    await flush()
    expect(global.fetch).toHaveBeenCalledTimes(2)

    gates[1].resolve(okResponse())
    await flush()
    expect(global.fetch).toHaveBeenCalledTimes(3)

    gates[2].resolve(okResponse())
    await Promise.all(calls)

    expect(maxInFlight).toBe(1)
    expect(paths).toEqual(['/status', '/presets', '/recorder'])
  })

  it('a rejected request does not stall the ones behind it', async () => {
    const first = api.request('GET', '/boom')
    const second = api.request('GET', '/after')

    await flush()
    gates[0].reject(new Error('network down'))
    await expect(first).rejects.toThrow('network down')

    await flush()
    expect(global.fetch).toHaveBeenCalledTimes(2)
    gates[1].resolve(okResponse('{"restored":true}'))
    await expect(second).resolves.toEqual({ restored: true })
    expect(maxInFlight).toBe(1)
  })

  it('still surfaces status and parsed body on a non-ok response', async () => {
    // The 409 lock/floor/pool rejections callers switch on must survive
    // being routed through the queue.
    const call = api.request('PUT', '/preset/active')
    await flush()
    gates[0].resolve({
      ok: false,
      status: 409,
      statusText: 'Conflict',
      text: async () => '{"error":"recording in progress"}',
    })

    await expect(call).rejects.toMatchObject({
      message: 'recording in progress',
      status: 409,
      body: { error: 'recording in progress' },
    })
  })
})

// A dropped connection is not an answer. fetch() rejects with a bare TypeError
// when the socket dies before a response arrives, and the device commits its
// change before it replies - so "Load failed" covers both "nothing happened"
// and "it happened and you did not hear about it". The analyzer produces these
// routinely: its RTA stream fragments the device heap below the 16KB a TLS
// handshake needs, and a request landing in the dip dies. Replaying the ones
// that are safe to replay is what keeps that invisible.
describe('api client transport retries', () => {
  let api
  let gates
  let paths

  // Fake timers keep the 250ms/750ms backoff out of the suite's runtime.
  const settle = () => vi.advanceTimersByTimeAsync(0)
  const runBackoff = () => vi.advanceTimersByTimeAsync(1000)

  beforeEach(() => {
    vi.useFakeTimers()
    api = new VybesAPI()
    gates = []
    paths = []
    global.fetch = vi.fn((url) => {
      paths.push(String(url).replace(api.baseUrl, ''))
      const gate = deferred()
      gates.push(gate)
      return gate.promise
    })
    vi.spyOn(console, 'error').mockImplementation(() => {})
    vi.spyOn(console, 'warn').mockImplementation(() => {})
  })

  afterEach(() => {
    vi.useRealTimers()
    vi.restoreAllMocks()
    delete global.fetch
  })

  it('replays an idempotent request whose connection dropped', async () => {
    const call = api.request('PUT', '/preset/output/eq?output=0', [{ freq: 100 }])

    await settle()
    gates[0].reject(new TypeError('Load failed'))
    await runBackoff()

    expect(global.fetch).toHaveBeenCalledTimes(2)
    gates[1].resolve(okResponse('{"ok":true}'))
    await expect(call).resolves.toEqual({ ok: true })
  })

  it('gives up after the configured attempts and reports the drop', async () => {
    const call = api.request('GET', '/preset?name=Phone%20mic')
    const settled = call.catch((e) => e)

    for (let i = 0; i < 3; i++) {
      await settle()
      gates[i].reject(new TypeError('Load failed'))
      await runBackoff()
    }

    expect(global.fetch).toHaveBeenCalledTimes(3)
    expect((await settled).message).toBe('Load failed')
  })

  it('never replays a POST - those are not idempotent', async () => {
    // POST /preset creates, /restore reboots, /recorder/record/start opens a
    // new file: a replay would do it twice.
    const call = api.request('POST', '/preset')
    const settled = call.catch((e) => e)

    await settle()
    gates[0].reject(new TypeError('Load failed'))
    await runBackoff()

    expect(global.fetch).toHaveBeenCalledTimes(1)
    expect((await settled).message).toBe('Load failed')
  })

  it('never replays an HTTP error - that is the device answering', async () => {
    const call = api.request('PUT', '/preset/output/fir')
    const settled = call.catch((e) => e)

    await settle()
    gates[0].resolve({
      ok: false, status: 409, statusText: 'Conflict',
      text: async () => '{"error":"tap pool exceeded"}',
    })
    await runBackoff()

    expect(global.fetch).toHaveBeenCalledTimes(1)
    expect((await settled).status).toBe(409)
  })

  it('retries inside the queue slot, so no second connection is opened', async () => {
    const first = api.request('PUT', '/volume')
    const second = api.request('GET', '/status')

    await settle()
    expect(paths).toEqual(['/volume'])

    gates[0].reject(new TypeError('Load failed'))
    await runBackoff()
    // The replay is in flight; the queued GET must still be waiting.
    expect(paths).toEqual(['/volume', '/volume'])

    gates[1].resolve(okResponse())
    await first
    await settle()
    expect(paths).toEqual(['/volume', '/volume', '/status'])

    gates[2].resolve(okResponse())
    await second
  })
})
