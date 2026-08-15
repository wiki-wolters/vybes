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

// Telemetry for GET /status. Internal RAM only (MALLOC_CAP_INTERNAL), which
// stays the meaningful number if PSRAM is ever enabled.
uint32_t healthFreeInternal();
uint32_t healthMinFreeInternal();
uint32_t healthLargestFreeBlock();
uint32_t healthMinLargestFreeBlock();
const char *healthLastRestartCause();
const char *healthResetReasonName();

#endif // HEALTH_H
