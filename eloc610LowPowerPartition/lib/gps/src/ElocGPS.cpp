/*
 * Created 2026 for the International Elephant Project (Wildlife Conservation International)
 *
 * The MIT License (MIT)
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
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
 *
 * NOTE: TinyGPS++ (mikalhart/TinyGPSPlus) is LGPL-2.1; the rest of ELOC is MIT. This driver only
 *       links against it.
 */

#include <time.h>
#include <driver/uart.h>
#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_timer.h>

#include "../../../include/project_config.h"
#include "ESP32Time.h"
#include "ElocSystem.hpp"
#include "ElocGPS.hpp"

static const char* TAG = "ElocGPS";

/// @brief Convert broken-down UTC time to a Unix epoch (seconds), without applying any local
///        timezone. Replaces timegm(), which is not available in ESP-IDF's newlib.
///        Uses Howard Hinnant's days-from-civil algorithm. Valid for years >= 1970.
static int64_t utc_tm_to_epoch(int year, int month, int day, int hour, int min, int sec) {
    // days_from_civil: days since 1970-01-01 (epoch)
    year -= (month <= 2);
    const int64_t era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(year - era * 400);            // [0, 399]
    const unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;  // [0, 365]
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;              // [0, 146096]
    const int64_t days = era * 146097 + static_cast<int64_t>(doe) - 719468;
    return days * 86400LL + hour * 3600LL + min * 60LL + sec;
}

// System clock, defined in main.cpp. Used to push GPS UTC time into the RTC.
extern ESP32Time timeObject;

// Lean by design: internal SRAM is nearly full.
#define GPS_RX_RING_BYTES   512   // NMEA @ 9600 baud ~= 960 B/s, drained continuously
#define GPS_TASK_STACK      3072
#define GPS_TASK_PRIO       2
#define GPS_TASK_CORE       0
#define GPS_READ_BUF        256   // local stack buffer for uart_read_bytes

ElocGPS::ElocGPS() :
    mGps(), mTaskHandle(nullptr), mInitialized(false), mLastUtcEpoch(0) {
}

ElocGPS::~ElocGPS() {
    deinit();
}

esp_err_t ElocGPS::init() {
    ESP_LOGI(TAG, "Func: %s", __func__);

    if (mInitialized) {
        ESP_LOGW(TAG, "GPS already initialized");
        return ESP_OK;
    }

    // Power on the module via the IO expander IO5 MOSFET gate (setGpsPower drives the active-low
    // gate low). Do this before the UART pins go active so the module sees VCC before any signal.
    if (ElocSystem::GetInstance().hasIoExpander()) {
        esp_err_t pwr = ElocSystem::GetInstance().getIoExpander().setGpsPower(true);
        if (pwr != ESP_OK) {
            ESP_LOGE(TAG, "Failed to enable GPS power via IO expander: %s", esp_err_to_name(pwr));
            // Continue anyway — the module may be powered externally on the bench.
        } else {
            ESP_LOGI(TAG, "GPS VCC enabled (IO expander IO5)");
        }
    } else {
        ESP_LOGW(TAG, "No IO expander present — cannot gate GPS power, assuming externally powered");
    }

    uart_config_t uart_config = {
        .baud_rate = GPS_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        // REF_TICK keeps the baud rate stable across dynamic frequency scaling (DFS is enabled).
        .source_clk = UART_SCLK_REF_TICK,
    };

    esp_err_t err;
    if ((err = uart_param_config(GPS_UART_PORT, &uart_config)) != ESP_OK) {
        ESP_LOGE(TAG, "uart_param_config failed: %s", esp_err_to_name(err));
        return err;
    }
    if ((err = uart_set_pin(GPS_UART_PORT, PIN_GPS_TX, PIN_GPS_RX,
                            UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE)) != ESP_OK) {
        ESP_LOGE(TAG, "uart_set_pin failed: %s", esp_err_to_name(err));
        return err;
    }
    // RX ring buffer only; no TX buffer (blocking writes), no event queue — keeps RAM use minimal.
    if ((err = uart_driver_install(GPS_UART_PORT, GPS_RX_RING_BYTES, 0, 0, nullptr, 0)) != ESP_OK) {
        ESP_LOGE(TAG, "uart_driver_install failed: %s", esp_err_to_name(err));
        return err;
    }

    BaseType_t ok = xTaskCreatePinnedToCore(&ElocGPS::taskWrapper, "gps", GPS_TASK_STACK,
                                            this, GPS_TASK_PRIO, &mTaskHandle, GPS_TASK_CORE);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create GPS task");
        uart_driver_delete(GPS_UART_PORT);
        return ESP_FAIL;
    }

    mInitialized = true;
    ESP_LOGI(TAG, "GPS initialized on UART%d (RX=GPIO%d, TX=GPIO%d, %d baud)",
             GPS_UART_PORT, PIN_GPS_RX, PIN_GPS_TX, GPS_UART_BAUD);
    return ESP_OK;
}

esp_err_t ElocGPS::deinit() {
    ESP_LOGI(TAG, "Func: %s", __func__);

    if (!mInitialized) {
        return ESP_OK;  // nothing to tear down
    }

    // 1) Stop the reader task first so nothing touches the UART while we remove it.
    if (mTaskHandle) {
        vTaskDelete(mTaskHandle);
        mTaskHandle = nullptr;
    }

    // 2) Remove the UART driver (frees the RX ring buffer and releases the pins from the UART).
    uart_driver_delete(GPS_UART_PORT);

    // 3) Drive the TX line (ESP -> GPS RXD, via R13) LOW *before* cutting VCC. If this line is left
    //    high while the module is gated off, current flows through the RXD ESD clamp diode into the
    //    VCC rail (~3.3V - Vdiode = ~2.6V), parasitically powering the chip and defeating the gate.
    //    Holding it low kills that path. RX (GPIO36) is input-only, so it sources no current.
    gpio_reset_pin(PIN_GPS_TX);
    gpio_set_direction(PIN_GPS_TX, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_GPS_TX, 0);

    // 4) Gate VCC off (IO5 high -> AO3401A P-channel MOSFET off).
    if (ElocSystem::GetInstance().hasIoExpander()) {
        esp_err_t pwr = ElocSystem::GetInstance().getIoExpander().setGpsPower(false);
        if (pwr != ESP_OK) {
            ESP_LOGE(TAG, "Failed to disable GPS power via IO expander: %s", esp_err_to_name(pwr));
        } else {
            ESP_LOGI(TAG, "GPS VCC disabled (IO expander IO5)");
        }
    }

    mInitialized = false;
    ESP_LOGI(TAG, "GPS powered down");
    return ESP_OK;
}

void ElocGPS::taskWrapper(void* arg) {
    static_cast<ElocGPS*>(arg)->gpsTask();
}

void ElocGPS::gpsTask() {
    ESP_LOGI(TAG, "GPS task started, searching for satellites...");

    uint8_t buf[GPS_READ_BUF];
    int64_t nextLogUs = esp_timer_get_time();   // log immediately on first pass, then every interval
    int64_t nextSyncUs = esp_timer_get_time();

    while (true) {
        int len = uart_read_bytes(GPS_UART_PORT, buf, sizeof(buf), pdMS_TO_TICKS(200));
        for (int i = 0; i < len; i++) {
            mGps.encode(buf[i]);
        }

        int64_t now = esp_timer_get_time();

        // Correct the clock from GPS UTC. Before the first successful sync, attempt on every pass so
        // a duty-cycle timer wake can grab the time the instant a fix lands (waitForTimeSync polls
        // mLastUtcEpoch) instead of waiting a full log interval. After that, re-sync periodically to
        // trim drift. syncTimeFromGps() returns early (and silently) until valid UTC is available, so
        // the per-pass attempts before lock cost almost nothing and never spam the log.
        if (mLastUtcEpoch == 0 || now >= nextSyncUs) {
            syncTimeFromGps();
            if (mLastUtcEpoch != 0) {
                nextSyncUs = now + static_cast<int64_t>(GPS_LOG_INTERVAL_S) * 1000000LL;
            }
        }

        if (now >= nextLogUs) {
            nextLogUs = now + static_cast<int64_t>(GPS_LOG_INTERVAL_S) * 1000000LL;
            logStatus();
        }
    }
}

esp_err_t ElocGPS::waitForTimeSync(uint32_t timeoutMs) {
    ESP_LOGI(TAG, "Func: %s", __func__);

    if (!mInitialized) {
        ESP_LOGW(TAG, "waitForTimeSync called but GPS is not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    const int64_t deadlineUs = esp_timer_get_time() + static_cast<int64_t>(timeoutMs) * 1000LL;
    while (mLastUtcEpoch == 0) {
        if (esp_timer_get_time() >= deadlineUs) {
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    return ESP_OK;
}

void ElocGPS::syncTimeFromGps() {
    if (!mGps.date.isValid() || !mGps.time.isValid()) {
        return;
    }
    // Sanity guard against pre-lock garbage dates (module may emit year 2000/2080 before almanac).
    if (mGps.date.year() < 2024 || mGps.date.year() > 2099) {
        return;
    }

    // GPS date/time fields are UTC. Convert directly to epoch without applying the active local
    // timezone (mktime would double-shift it).
    int64_t epoch = utc_tm_to_epoch(mGps.date.year(), mGps.date.month(), mGps.date.day(),
                                    mGps.time.hour(), mGps.time.minute(), mGps.time.second());
    if (epoch <= 0) {
        return;
    }

    // GPS provides UTC only — set the absolute epoch and leave the configured TZ offset untouched.
    // (ESP32Time::setTime's sub-second arg is clamped to 0..999, so pass 0.)
    timeObject.setTime(static_cast<long>(epoch), 0);
    mLastUtcEpoch = epoch;
    ESP_LOGI(TAG, "Time synced from GPS (UTC epoch=%ld) -> local %s",
             static_cast<long>(epoch), timeObject.getDateTime().c_str());
}

void ElocGPS::logStatus() {
    if (mGps.location.isValid()) {
        ESP_LOGI(TAG, "GPS fix: lat=%.6f lon=%.6f alt=%.1fm sats=%lu hdop=%.1f (age=%lums)",
                 mGps.location.lat(), mGps.location.lng(), mGps.altitude.meters(),
                 static_cast<unsigned long>(mGps.satellites.value()), mGps.hdop.hdop(),
                 static_cast<unsigned long>(mGps.location.age()));
    } else {
        ESP_LOGI(TAG, "GPS: no fix yet (sats=%lu, chars=%lu, sentences=%lu, csum_err=%lu)",
                 static_cast<unsigned long>(mGps.satellites.value()),
                 static_cast<unsigned long>(mGps.charsProcessed()),
                 static_cast<unsigned long>(mGps.sentencesWithFix()),
                 static_cast<unsigned long>(mGps.failedChecksum()));
    }
}
