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
