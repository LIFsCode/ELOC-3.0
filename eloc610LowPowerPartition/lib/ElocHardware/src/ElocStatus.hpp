/*
 * Created on Sun Nov 05 2023
 *
 * Project: International Elephant Project (Wildlife Conservation International)
 *
 * The MIT License (MIT)
 * Copyright (c) 2023 Fabian Lindner
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software
 * and associated documentation files (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED
 * TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */


#ifndef ELOCSTATUS_HPP_
#define ELOCSTATUS_HPP_

#include <stdint.h>
#include "WString.h"
#include "WAVFileWriter.h"

//TODO: All these variables are shared across multiple tasks and must be guarded with mutexes


/* Recording specific status indicators */
extern WAVFileWriter wav_writer;
extern bool ai_run_enable;

#ifdef EDGE_IMPULSE_ENABLED
    #include "EdgeImpulse.hpp"
    extern EdgeImpulse edgeImpulse;
#endif

extern int64_t gTotalUPTimeSinceReboot;  //esp_timer_get_time returns 64-bit time since startup, in microseconds.
extern int64_t gTotalRecordTimeSinceReboot;
extern int64_t gSessionRecordTime;
extern String gSessionIdentifier;
extern String gFirmwareVersion;

/* Deferred AI start mechanism:
 * When AI mode is enabled via BT command, the actual AI thread start is deferred
 * by a few seconds to allow BT to serve follow-up commands (getStatus/getConfig)
 * before the AI thread consumes most CPU/memory resources.
 */
extern bool g_ai_start_pending;
extern int64_t g_ai_deferred_start_time;

/// @brief Delay in microseconds before actually starting the AI thread after setRecordMode
#define AI_DEFERRED_START_DELAY_US  3000000LL  // 3 seconds

/*******************************************************************************
 * Duty-Cycle Deep Sleep State Machine
 ******************************************************************************/

/// @brief Magic number to validate RTC duty cycle state (0xE10CDC1E = "ELOC DC IE")
#define DUTY_CYCLE_RTC_MAGIC 0xE10CDC1E

/// @brief Sleep cycle state machine states
typedef enum {
    SLEEP_CYCLE_DISABLED = 0,       // Normal operation (duty cycle off)
    SLEEP_CYCLE_INFERENCE_ACTIVE,   // Awake, running AI inference
    SLEEP_CYCLE_PREPARING,          // Shutting down for sleep
    SLEEP_CYCLE_ENTERING_SLEEP      // About to enter deep sleep
} SleepCycleState_t;

/// @brief RTC-persistent state that survives deep sleep
///        Contains both duty cycle counters and LoRa event cooldown state
typedef struct {
    uint32_t magic;                  // Validation: DUTY_CYCLE_RTC_MAGIC
    uint32_t bootCount;              // Wake-up counter
    uint32_t totalDetections;        // Running detection count across cycles
    int64_t  lastEventLoraTimeS;     // Cooldown: when last event LoRa sent (epoch seconds)
    int64_t  lastStatusLoraTimeS;    // Heartbeat: when last status uplink sent (epoch seconds)
    int64_t  lastDetectionTimeS;     // Cooldown: when last detection occurred (epoch seconds)
    uint8_t  eventState;             // EVENT_IDLE(0) or EVENT_ACTIVE(1)
    uint32_t detectionsSinceLastMsg; // Aggregation counter for ongoing msgs
    int64_t  eventStartTimeS;        // When current event session started (epoch seconds)
    char     sessionId[80];          // Session folder name, persisted across sleep cycles
    int8_t   timezoneOffset;         // User-set (app/BT) TZ offset (hours, -12..14), survives deep sleep
    bool     timezoneOffsetValid;    // false until BT setTime supplies one
    uint8_t  recordMode;             // WAVFileWriter::Mode to restore on wake (0=disabled,1=continuous,2=single)
    bool     aiEnabled;              // whether AI inference should auto-start on wake
    int64_t  lastGpsSyncS;           // when the clock was last set from GPS UTC (epoch seconds, 0=never)
    int8_t   gpsTimezoneOffset;      // TZ offset derived from GPS longitude (used when no app TZ is set)
    bool     gpsTimezoneValid;       // false until a GPS-longitude offset has been derived at least once
} rtc_duty_cycle_t;

/// @brief Global sleep cycle state (non-persistent, reset each boot)
extern SleepCycleState_t gSleepCycleState;

/// @brief Timestamp (microseconds, esp_timer_get_time) when duty cycle was activated
///        Used to measure awake duration from activation, not from boot
extern int64_t gDutyCycleActivationTimeUS;

/// @brief Whether this boot was a timer wake from duty cycle deep sleep
extern bool gIsTimerWake;

/// @brief RTC-persistent duty cycle state (survives deep sleep)
extern rtc_duty_cycle_t rtc_duty_cycle;

#endif // ELOCSTATUS_HPP_
