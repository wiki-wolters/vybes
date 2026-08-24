#ifndef HEALTH_H
#define HEALTH_H

#include <Arduino.h>

// Liveness watchdog and heap telemetry.
//
// The stock watchdogs cannot save this device from the failure it actually
// hits. CONFIG_ESP_TASK_WDT is on with panic, but it only watches the CPU0
// idle task (CHECK_IDLE_TASK_CPU1 is unset, and Arduino's loopTask runs on
// core 1), and more importantly it detects *starvation* - a task hogging the
// CPU. Heap exhaustion produces the opposite: tasks blocked on allocations
// that never succeed, the idle task still scheduled, the watchdog satisfied,
// and the chip sitting there until someone pulls EN (observed 2026-08-15).
//
// So this module runs its own monitor task on core 0 and restarts the device
// when it stops being useful, recording why in RTC memory so the next boot
// can report it. Everything it measures is also exposed on GET /status - the
// point is to make weeks-long uptime observable, not just hoped for.

void initHealth();                       // early in setup(): report last restart
void startHealthMonitor(bool standalone); // end of setup(): start the monitor
void healthBeat();                       // in loop(): the liveness heartbeat
void healthListenersReady();             // setupWebServer(): listeners are up

// Telemetry for GET /status. Internal RAM only (MALLOC_CAP_INTERNAL), which
// stays the meaningful number if PSRAM is ever enabled.
uint32_t healthFreeInternal();
uint32_t healthMinFreeInternal();
uint32_t healthLargestFreeBlock();
uint32_t healthMinLargestFreeBlock();
// PSRAM, reported so it is possible to tell that it is actually carrying the
// TLS buffers rather than merely being linked in. Both read 0 on a build
// without it, which is itself the check that memory_type took effect.
uint32_t healthPsramFree();
uint32_t healthPsramSize();
const char *healthLastRestartCause();
const char *healthResetReasonName();

// Repeat-failure escalation. Two watchdog restarts in a row bring the device
// up with HTTPS disabled, so it stays reachable on port 80 instead of wedging
// again on the load that just killed it. Read by setupWebServer(); clears
// itself once the device holds a healthy uptime.
bool healthDegradedMode();
uint32_t healthRestartStreak();

#endif // HEALTH_H
