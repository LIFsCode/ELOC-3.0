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
 */

/**
 * @file ElocGPS.hpp
 * @brief Driver for the ATGM336H GNSS module.
 *
 * Powers the module via the PCA9557 IO expander (IO5 MOSFET gate), reads NMEA over UART, parses it
 * with TinyGPS++, and syncs the system clock from GPS UTC. A dedicated FreeRTOS task drains the UART
 * and logs position + time to the serial monitor every GPS_LOG_INTERVAL_S seconds.
 *
 * Singleton, mirroring ElocSystem / ElocLora. Lightweight by design — internal SRAM is nearly full.
 */

#ifndef ELOCGPS_HPP_
#define ELOCGPS_HPP_

#include <stdint.h>
#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <TinyGPS++.h>

class ElocGPS {
public:
    inline static ElocGPS& GetInstance() {
        static ElocGPS instance;
        return instance;
    }

    /// @brief Power on the GPS module, install the UART driver and start the reader task.
    /// @return ESP_OK on success, error code otherwise
    esp_err_t init();

    /// @brief Power the GPS module down cleanly. Stops the reader task, removes the UART driver,
    ///        drives the TX line (ESP -> GPS RXD) LOW so the module cannot back-feed itself through
    ///        its RXD ESD clamp while VCC is gated off, then switches the VCC MOSFET off (IO5 high).
    ///        Safe to call when not initialized (no-op). Suitable for duty-cycle / deep-sleep paths.
    /// @return ESP_OK on success, error code otherwise
    esp_err_t deinit();

    /// @brief Whether the GPS is currently powered and running.
    inline bool isInitialized() const { return mInitialized; }

    // --- Status getters (for later getStatus / LoRa integration) ---
    // Note: TinyGPS++ accessors are non-const, so these cannot be const-qualified.
    inline bool hasFix() { return mGps.location.isValid(); }
    inline double getLat() { return mGps.location.lat(); }
    inline double getLng() { return mGps.location.lng(); }
    inline uint32_t getSatellites() { return mGps.satellites.value(); }
    inline int64_t lastUtcEpoch() const { return mLastUtcEpoch; }

private:
    ElocGPS();
    ~ElocGPS();
    ElocGPS(const ElocGPS&) = delete;
    ElocGPS& operator=(const ElocGPS&) = delete;

    static void taskWrapper(void* arg);
    void gpsTask();

    /// @brief If a valid UTC date+time is available, push it to the system clock (timeObject).
    void syncTimeFromGps();

    /// @brief Emit the periodic position + time status line to the serial log.
    void logStatus();

    TinyGPSPlus mGps;
    TaskHandle_t mTaskHandle;
    bool mInitialized;
    int64_t mLastUtcEpoch;   // epoch seconds of the last successful GPS time sync (0 = never)
};

#endif // ELOCGPS_HPP_
