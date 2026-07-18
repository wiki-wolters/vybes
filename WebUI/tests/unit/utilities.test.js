import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest'
import { asyncDebounce, debounce, throttleAndDebounce } from '../../src/utilities.js'

beforeEach(() => {
  vi.useFakeTimers()
})

afterEach(() => {
  vi.useRealTimers()
})

describe('asyncDebounce', () => {
  it('collapses rapid calls into one invocation with the last args', async () => {
    const fn = vi.fn(async (x) => x * 2)
    const debounced = asyncDebounce(fn, 200)

    const p1 = debounced(1)
    const p2 = debounced(2)
    const p3 = debounced(3)

    expect(fn).not.toHaveBeenCalled()
    await vi.advanceTimersByTimeAsync(200)

    expect(fn).toHaveBeenCalledTimes(1)
    expect(fn).toHaveBeenCalledWith(3)
    await expect(Promise.all([p1, p2, p3])).resolves.toEqual([6, 6, 6])
  })

  it('settles ALL callers: superseded calls resolve with the final result', async () => {
    const fn = vi.fn(async (x) => `result-${x}`)
    const debounced = asyncDebounce(fn, 200)

    const early = debounced('early')
    await vi.advanceTimersByTimeAsync(100) // not yet fired
    const late = debounced('late')
    await vi.advanceTimersByTimeAsync(200)

    // The early promise must not hang; it shares the final invocation's result
    await expect(early).resolves.toBe('result-late')
    await expect(late).resolves.toBe('result-late')
    expect(fn).toHaveBeenCalledTimes(1)
  })

  it('propagates rejection to every pending caller', async () => {
    const boom = new Error('boom')
    const fn = vi.fn(async () => { throw boom })
    const debounced = asyncDebounce(fn, 200)

    const p1 = debounced('a')
    const p2 = debounced('b')
    // Attach handlers before advancing so no unhandled rejection escapes
    const results = Promise.allSettled([p1, p2])
    await vi.advanceTimersByTimeAsync(200)

    const settled = await results
    expect(settled[0]).toEqual({ status: 'rejected', reason: boom })
    expect(settled[1]).toEqual({ status: 'rejected', reason: boom })
    expect(fn).toHaveBeenCalledTimes(1)
  })

  it('a rejection does not poison later calls', async () => {
    let shouldFail = true
    const fn = vi.fn(async (x) => {
      if (shouldFail) throw new Error('first fails')
      return x
    })
    const debounced = asyncDebounce(fn, 200)

    const failing = debounced('a')
    const failResult = failing.catch((e) => e.message)
    await vi.advanceTimersByTimeAsync(200)
    expect(await failResult).toBe('first fails')

    shouldFail = false
    const ok = debounced('b')
    await vi.advanceTimersByTimeAsync(200)
    await expect(ok).resolves.toBe('b')
    expect(fn).toHaveBeenCalledTimes(2)
  })

  it('separate instances are independent', async () => {
    const fnA = vi.fn(async (x) => `A:${x}`)
    const fnB = vi.fn(async (x) => `B:${x}`)
    const debouncedA = asyncDebounce(fnA, 200)
    const debouncedB = asyncDebounce(fnB, 200)

    const pa = debouncedA(1)
    const pb = debouncedB(2)
    // A call on B must not reset or cancel A's pending timer/waiters
    await vi.advanceTimersByTimeAsync(150)
    const pb2 = debouncedB(3)
    await vi.advanceTimersByTimeAsync(200)

    await expect(pa).resolves.toBe('A:1')
    await expect(pb).resolves.toBe('B:3')
    await expect(pb2).resolves.toBe('B:3')
    expect(fnA).toHaveBeenCalledTimes(1)
    expect(fnB).toHaveBeenCalledTimes(1)
  })

  it('calls spaced beyond the wait each invoke separately', async () => {
    const fn = vi.fn(async (x) => x)
    const debounced = asyncDebounce(fn, 200)

    const p1 = debounced(1)
    await vi.advanceTimersByTimeAsync(200)
    const p2 = debounced(2)
    await vi.advanceTimersByTimeAsync(200)

    await expect(p1).resolves.toBe(1)
    await expect(p2).resolves.toBe(2)
    expect(fn).toHaveBeenCalledTimes(2)
  })
})

describe('debounce', () => {
  it('collapses rapid calls into one trailing invocation with the last args', () => {
    const fn = vi.fn()
    const debounced = debounce(fn, 200)

    debounced(1)
    debounced(2)
    debounced(3)
    expect(fn).not.toHaveBeenCalled()

    vi.advanceTimersByTime(200)
    expect(fn).toHaveBeenCalledTimes(1)
    expect(fn).toHaveBeenCalledWith(3)
  })

  it('a new call within the wait restarts the timer', () => {
    const fn = vi.fn()
    const debounced = debounce(fn, 200)

    debounced('a')
    vi.advanceTimersByTime(150)
    debounced('b')
    vi.advanceTimersByTime(150)
    expect(fn).not.toHaveBeenCalled()

    vi.advanceTimersByTime(50)
    expect(fn).toHaveBeenCalledTimes(1)
    expect(fn).toHaveBeenCalledWith('b')
  })

  it('separate instances are independent', () => {
    const fnA = vi.fn()
    const fnB = vi.fn()
    const a = debounce(fnA, 200)
    const b = debounce(fnB, 200)

    a(1)
    b(2)
    vi.advanceTimersByTime(200)
    expect(fnA).toHaveBeenCalledWith(1)
    expect(fnB).toHaveBeenCalledWith(2)
  })
})

describe('throttleAndDebounce', () => {
  it('executes immediately on the first call (leading edge)', () => {
    const fn = vi.fn()
    const wrapped = throttleAndDebounce(fn, 500, 200)

    wrapped('first')
    expect(fn).toHaveBeenCalledTimes(1)
    expect(fn).toHaveBeenCalledWith('first')
  })

  it('throttles rapid calls but always fires a trailing save with the last args', () => {
    const fn = vi.fn()
    const wrapped = throttleAndDebounce(fn, 500, 200)

    wrapped(1) // leading: fires immediately
    expect(fn).toHaveBeenCalledTimes(1)

    vi.advanceTimersByTime(100)
    wrapped(2) // within throttle window: deferred
    vi.advanceTimersByTime(100)
    wrapped(3) // still within: replaces pending debounce
    expect(fn).toHaveBeenCalledTimes(1)

    // The trailing debounce (200ms after the last call) delivers the final value
    vi.advanceTimersByTime(200)
    expect(fn).toHaveBeenCalledTimes(2)
    expect(fn).toHaveBeenLastCalledWith(3)
  })

  it('a call after the throttle window fires immediately again', () => {
    const fn = vi.fn()
    const wrapped = throttleAndDebounce(fn, 500, 200)

    wrapped('a')
    expect(fn).toHaveBeenCalledTimes(1)

    // Let all trailing work settle, then move past the throttle window
    vi.advanceTimersByTime(1000)
    const callsBefore = fn.mock.calls.length

    wrapped('b')
    expect(fn.mock.calls.length).toBe(callsBefore + 1)
    expect(fn).toHaveBeenLastCalledWith('b')
  })
})
