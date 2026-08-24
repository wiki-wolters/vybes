#include "health.h"
#include "globals.h"
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Internal RAM is the scarce resource, not the combined heap ESP.getFreeHeap()
// reports, so every measurement here is explicitly MALLOC_CAP_INTERNAL.
#define HEALTH_CAPS (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)

// --- restart thresholds ----------------------------------------------------
// These are deliberately far below any legitimate operating point, because a
// spurious restart costs more than a slow wedge now that the telemetry below
// makes a real one visible. Measured 2026-08-15 on the live device: idle
// internal heap ~131KB, and TWO concurrent TLS connections plus a browser
// websocket drove minFreeInternal to 20,420 and minLargestFreeBlock to
// 15,348. Steady-state readings badly understate the risk - the danger is the
// transient peak inside a handshake, which only the watermarks reveal. The
// heap floor sits under even that transient; the fragmentation floor cannot
// (see below), so it leans on its grace period instead. Both need the fault
// *sustained* before acting. Retune once there is long-uptime data.
static const uint32_t HEAP_FLOOR_BYTES = 12 * 1024;
static const uint32_t HEAP_FLOOR_GRACE_MS = 15000;
// A TLS handshake needs a 16KB contiguous buffer. Free heap can read a healthy
// 130KB while the largest block has fragmented below that - the RTA streams
// ~10Hz of String/JSON churn whenever the analyzer is open - at which point
// HTTPS is dead even though nothing looks wrong.
//
// This floor must sit ABOVE the 16KB a handshake needs, not below it. The old
// 10KB left a blind band: a largest block anywhere in 10-16KB is too small to
// hand out a handshake buffer yet too large to trip the check, so HTTPS would
// be dead with every test here satisfied - loop() beating, WiFi connected,
// freeInternal a healthy 120KB. 20KB clears the handshake requirement with
// room for the allocator's own overhead.
//
// What was actually measured, though, is dips rather than a park. Polled every
// 15s on 2026-08-20 (steady largest ~53KB, freeInternal ~120KB) the watermarks
// reached minLargestFreeBlock=2,292 and minFreeInternal=11,680 - the latter
// under HEAP_FLOOR_BYTES - with no restart and uptime climbing unbroken. So
// both floors are being breached transiently and recovering. The 250ms sampler
// misses most of it; those watermarks come from healthLargestFreeBlock()
// folding one in on every call, including GET /status. A handshake unlucky
// enough to land inside a dip fails on its own, which reads as intermittent
// HTTPS rather than a wedge - worth separating from the blind band above when
// diagnosing, because raising this floor does nothing for it.
//
// Sitting above the transient is still safe: sustained() zeroes `since` the
// moment the fault clears, so tripping this needs the largest block
// continuously under 20KB for the whole 120s grace (~480 consecutive 250ms
// samples). Millisecond dips cannot accumulate; a stuck state will.
static const uint32_t FRAG_FLOOR_BYTES = 20 * 1024;
static const uint32_t FRAG_FLOOR_GRACE_MS = 120000;
static const uint32_t WIFI_DOWN_GRACE_MS = 120000;
// loop() is non-blocking throughout (teensyCommLoop drains a queue, the FIR
// upload runs in the httpd task), so 30s of no heartbeat means a deadlock -
// the non-recursive config mutex being the obvious candidate.
static const uint32_t LOOP_STALL_GRACE_MS = 30000;
// --- when the watchdog arms ------------------------------------------------
// A flat 60s boot grace - what this was until 2026-08-24 - is what turned a
// working restart into a dead device. Measured that morning: the heap floor
// fired at 09:01:05, the device came back clean, every client reconnected
// within ~1s (the boot log shows a websocket attach seconds in), and it wedged
// again 35s later - inside the grace, with every check switched off. It never
// recovered and needed a manual EN pull 75 minutes later.
//
// So arm off the event the grace actually existed for - the listeners coming
// up, the heaviest allocation of the device's life - rather than wall-clock
// guesswork, and let ARM_AFTER_LISTENERS_MS cover the settling after that.
// Every check still needs its own fault *sustained* on top (the heap floor
// wants 15s), so a boot transient cannot trip anything either way.
// ARM_FALLBACK_MS is the backstop: if the ready signal never arrives - no
// certs, a listener that failed to start, a refactor that drops the call -
// the watchdog must not sit disarmed forever.
static const uint32_t ARM_AFTER_LISTENERS_MS = 15000;
static const uint32_t ARM_FALLBACK_MS = 60000;

// --- repeat-failure escalation ---------------------------------------------
// Two watchdog restarts in a row means the load that exhausted the heap is
// still out there and will do it again the moment the listeners come up.
// Coming back with HTTPS off keeps the device reachable: ~40KB per TLS socket
// is the whole problem and plain HTTP costs none of it, so the next boot can
// be diagnosed over port 80 instead of needing someone at the EN pin. This is
// a deliberately visible degradation - the phone needs a secure context for
// its mic, so the delay probe and auto-EQ stop working until it clears.
// STREAK_DECAY_MS is what clears it: hold a healthy uptime that long and the
// streak resets, so two unrelated restarts weeks apart never accumulate into
// a permanent downgrade.
static const uint32_t DEGRADE_AFTER_RESTARTS = 2;
static const uint32_t STREAK_DECAY_MS = 600000; // 10 min healthy = forgiven

static const uint32_t HEALTH_LOG_INTERVAL_MS = 300000; // 5 min serial summary

// --- restart cause, carried across the restart -----------------------------
// RTC_NOINIT_ATTR survives esp_restart() (unlike RTC_DATA_ATTR, which the
// bootloader re-initialises); it holds garbage after a cold power-on, hence
// the magic. esp_reset_reason() will only say "software restart" - this says
// which check pulled the trigger.
#define HEALTH_MAGIC 0x56594244UL // "VYBD"

enum HealthCause : uint32_t {
    CAUSE_NONE = 0,
    CAUSE_HEAP_FLOOR,
    CAUSE_FRAGMENTATION,
    CAUSE_WIFI_DOWN,
    CAUSE_LOOP_STALL,
};

RTC_NOINIT_ATTR static uint32_t rtcMagic;
RTC_NOINIT_ATTR static uint32_t rtcCause;
// Consecutive watchdog restarts, carried across the restart beside the cause.
// The cause is consumed by the boot that reports it; this is not - only a
// healthy uptime clears it. See DEGRADE_AFTER_RESTARTS.
RTC_NOINIT_ATTR static uint32_t rtcStreak;

static const char *causeName(uint32_t cause) {
    switch (cause) {
        case CAUSE_HEAP_FLOOR:    return "internal heap floor";
        case CAUSE_FRAGMENTATION: return "heap fragmentation";
        case CAUSE_WIFI_DOWN:     return "wifi down";
        case CAUSE_LOOP_STALL:    return "loop stall";
        default:                  return "none";
    }
}

static const char *lastRestartCause = "none";
static bool healthStandalone = false;
static volatile uint32_t loopHeartbeat = 0;
static volatile uint32_t minLargestBlock = UINT32_MAX;
static volatile uint32_t listenersReadyAt = 0;
static bool degradedMode = false;

const char *healthResetReasonName() {
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON:   return "power-on";
        case ESP_RST_EXT:       return "external reset pin";
        case ESP_RST_SW:        return "software restart";
        case ESP_RST_PANIC:     return "panic / exception";
        case ESP_RST_INT_WDT:   return "interrupt watchdog";
        case ESP_RST_TASK_WDT:  return "task watchdog";
        case ESP_RST_WDT:       return "other watchdog";
        case ESP_RST_DEEPSLEEP: return "deep sleep wake";
        case ESP_RST_BROWNOUT:  return "brownout";
        case ESP_RST_SDIO:      return "SDIO";
        default:                return "unknown";
    }
}

uint32_t healthFreeInternal() { return heap_caps_get_free_size(HEALTH_CAPS); }
uint32_t healthMinFreeInternal() { return heap_caps_get_minimum_free_size(HEALTH_CAPS); }

// Records the low-water mark on every call, deliberately. The allocator keeps
// its own minimum for free size, but not for the largest *contiguous* block,
// so we have to watch that ourselves - and a plain 1Hz sampler is nearly
// certain to miss the moments worth catching, since fragmentation pressure
// peaks for a few ms inside a TLS handshake. Folding the watermark into the
// getter means every observation counts, including the one GET /status takes
// while it is itself being served over TLS.
uint32_t healthLargestFreeBlock() {
    uint32_t largest = heap_caps_get_largest_free_block(HEALTH_CAPS);
    if (largest < minLargestBlock) {
        minLargestBlock = largest;
    }
    return largest;
}

uint32_t healthMinLargestFreeBlock() {
    uint32_t low = minLargestBlock;
    return low == UINT32_MAX ? healthLargestFreeBlock() : low;
}
const char *healthLastRestartCause() { return lastRestartCause; }

void healthListenersReady() {
    // First call wins: a retry, or a second caller added later, must not push
    // the arming point back and re-open the window this exists to close.
    if (listenersReadyAt == 0) {
        listenersReadyAt = millis();
    }
}

bool healthDegradedMode() { return degradedMode; }

uint32_t healthRestartStreak() {
    return rtcMagic == HEALTH_MAGIC ? rtcStreak : 0;
}

void initHealth() {
    // A cold power-on leaves all three of these holding garbage, so the magic
    // is what separates "carried across a restart" from "never initialised".
    if (rtcMagic != HEALTH_MAGIC) {
        rtcMagic = HEALTH_MAGIC;
        rtcCause = CAUSE_NONE;
        rtcStreak = 0;
    }

    if (rtcCause != CAUSE_NONE) {
        lastRestartCause = causeName(rtcCause);
        DebugSerial.printf("Health watchdog restarted the device last boot: %s "
                           "(%u in a row)\n", lastRestartCause,
                           (unsigned)rtcStreak);
    }

    // Decided before the cause is consumed below, because the streak is the
    // part that survives - and setupWebServer() reads this to skip HTTPS.
    degradedMode = rtcStreak >= DEGRADE_AFTER_RESTARTS;
    if (degradedMode) {
        DebugSerial.printf("Health: %u watchdog restarts in a row - starting "
                           "DEGRADED, HTTPS disabled this boot. Clears after "
                           "%lus of healthy uptime.\n", (unsigned)rtcStreak,
                           (unsigned long)(STREAK_DECAY_MS / 1000));
    }

    // Consume the cause so the next boot cannot report it as its own. The
    // streak deliberately stays put; only a healthy uptime clears that.
    rtcCause = CAUSE_NONE;
}

void healthBeat() { loopHeartbeat++; }

static void restartWithCause(HealthCause cause, const char *detail) {
    DebugSerial.printf("Health watchdog: %s (%s) - restarting. "
                       "freeInternal=%u largestBlock=%u uptime=%lus\n",
                       causeName(cause), detail,
                       (unsigned)healthFreeInternal(),
                       (unsigned)healthLargestFreeBlock(),
                       (unsigned long)(millis() / 1000));
    rtcMagic = HEALTH_MAGIC;
    rtcCause = cause;
    // initHealth() has already validated the RTC block, and the monitor task
    // that calls this only starts after it, so this increments a known value
    // rather than power-on garbage.
    rtcStreak++;
    DebugSerial.flush();
    delay(100);
    ESP.restart();
}

// Returns true once `now` has been at or past a fault for `graceMs`. `since`
// holds the first sighting (0 = not currently faulted) so a transient dip
// during a handshake burst resets instead of accumulating.
static bool sustained(bool faulted, uint32_t &since, uint32_t now, uint32_t graceMs) {
    if (!faulted) {
        since = 0;
        return false;
    }
    if (since == 0) {
        since = now;
        return false;
    }
    return (now - since) >= graceMs;
}

// True once the listeners have been up long enough to settle, or once the
// fallback expires so a missing ready signal cannot leave this disarmed.
static bool watchdogArmed(uint32_t now) {
    if (now >= ARM_FALLBACK_MS) {
        return true;
    }
    uint32_t ready = listenersReadyAt;
    return ready != 0 && (now - ready) >= ARM_AFTER_LISTENERS_MS;
}

static void healthMonitorTask(void *) {
    uint32_t heapLowSince = 0, fragLowSince = 0, wifiDownSince = 0;
    uint32_t lastBeat = loopHeartbeat;
    uint32_t lastBeatChange = millis();
    uint32_t lastLog = millis();

    for (;;) {
        // 250ms rather than a second: the grace periods below are all in
        // seconds so the tick rate does not affect them, but a faster sampler
        // has a better chance of catching a fragmentation dip that only lasts
        // as long as a handshake. Four heap_caps calls a second is nothing.
        vTaskDelay(pdMS_TO_TICKS(250));

        uint32_t now = millis();
        uint32_t freeInternal = healthFreeInternal();
        uint32_t largest = healthLargestFreeBlock(); // also records the watermark

        uint32_t beat = loopHeartbeat;
        if (beat != lastBeat) {
            lastBeat = beat;
            lastBeatChange = now;
        }

        if (now - lastLog >= HEALTH_LOG_INTERVAL_MS) {
            lastLog = now;
            DebugSerial.printf("Health: uptime=%lus freeInternal=%u minFree=%u "
                               "largestBlock=%u minLargest=%u\n",
                               (unsigned long)(now / 1000), (unsigned)freeInternal,
                               (unsigned)healthMinFreeInternal(), (unsigned)largest,
                               (unsigned)healthMinLargestFreeBlock());
        }

        // A healthy stretch forgives the past, so restarts weeks apart never
        // add up to a permanent downgrade. Deliberately ahead of the arming
        // gate: forgiving is always safe, whatever the watchdog's state.
        if (rtcStreak != 0 && now >= STREAK_DECAY_MS) {
            DebugSerial.printf("Health: %lus healthy - restart streak cleared\n",
                               (unsigned long)(now / 1000));
            rtcStreak = 0;
        }

        // Arm off the listeners being up, not a flat boot timer - see
        // ARM_AFTER_LISTENERS_MS for why the old 60s window was the bug.
        if (!watchdogArmed(now)) {
            continue;
        }

        if (sustained(freeInternal < HEAP_FLOOR_BYTES, heapLowSince, now,
                      HEAP_FLOOR_GRACE_MS)) {
            restartWithCause(CAUSE_HEAP_FLOOR, "internal heap exhausted");
        }
        if (sustained(largest < FRAG_FLOOR_BYTES, fragLowSince, now,
                      FRAG_FLOOR_GRACE_MS)) {
            restartWithCause(CAUSE_FRAGMENTATION, "no contiguous block for TLS");
        }
        // Standalone AP mode is never WL_CONNECTED - there is no router to be
        // disconnected from, so the check does not apply.
        if (!healthStandalone &&
            sustained(WiFi.status() != WL_CONNECTED, wifiDownSince, now,
                      WIFI_DOWN_GRACE_MS)) {
            restartWithCause(CAUSE_WIFI_DOWN, "no reconnect");
        }
        if (now - lastBeatChange >= LOOP_STALL_GRACE_MS) {
            restartWithCause(CAUSE_LOOP_STALL, "loop() stopped advancing");
        }
    }
}

void startHealthMonitor(bool standalone) {
    healthStandalone = standalone;
    // Core 0, low priority: the Arduino loop task owns core 1, so a wedged or
    // busy loop cannot starve the thing whose job is to notice.
    xTaskCreatePinnedToCore(healthMonitorTask, "health", 3072, NULL, 1, NULL, 0);
}
