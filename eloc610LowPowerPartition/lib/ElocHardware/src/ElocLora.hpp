/*
 * Created on Mon Dec 23 2024
 *
 * Project: International Elephant Project (Wildlife Conservation International)
 *
 * The MIT License (MIT)
 * Copyright (c) 2024 Fabian Lindner
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
#ifndef ELOCLORA_HPP_
#define ELOCLORA_HPP_

#include <esp_err.h>


#include "RadioLib.h"
#include <Arduino.h>
#include <SPI.h>

#define LORA_MAX_RX_PAYLOAD 20
#define LORA_MAX_TX_PAYLOAD 30
#define LORA_LABEL_LEN 5
// if anything in the lora payload format is changed which is incompatible with existing payload fields
// the version must be increasd. If only additinal bytes are added this is not mandatory
#define LORA_MSG_VERS 0

// LoRaWAN session persistence constants
#define LORAWAN_SESSION_MAGIC 0x4C4F5241  // "LORA" in ASCII
#define RADIOLIB_LORAWAN_SESSION_BUF_SIZE 512
#define RADIOLIB_LORAWAN_NONCES_BUF_SIZE 56

// Structure for RTC memory session persistence
typedef struct {
    uint32_t magic;
    uint32_t crc32;
    uint16_t sessionSize;
    uint8_t sessionData[RADIOLIB_LORAWAN_SESSION_BUF_SIZE];
    uint8_t noncesData[RADIOLIB_LORAWAN_NONCES_BUF_SIZE];
} rtc_lorawan_session_t;

class ElocLora
{
private:
    enum t_LoraMsgType {
        STATUS_MSG = 0,
        EVENT_MSG = 1,
        INTRUDER_MSG = 2,
    };


    bool mInitDone;
    /* data */
    SPIClass loraSPI;
    // RADIOLIB_DEFAULT_SPI_SETTINGS
    const SPISettings loraSpiSettings = SPISettings(2000000, MSBFIRST, SPI_MODE0);

    // first you have to set your radio model and pin configuration
    // this is provided just as a default example
    // config for TTGO T3 V1.6.1 (V2.1) https://github.com/LilyGO/TTGO-LoRa32-V2.1
    SX1262 radio;
    
    // how often to send an uplink - consider legal & FUP constraints - see notes
    uint32_t uplinkIntervalSeconds = 24UL * 60UL * 60UL;    // 24 hours (default heartbeat interval)
    // last raw config value seen by refreshUplinkInterval(), to log validation warnings only once
    int64_t mLastCfgUplinkIntervalS = -1;

    /// @brief Re-read upLinkIntervalS from the config (with min-interval validation) so
    ///        setConfig changes apply without a reboot.
    void refreshUplinkInterval();

    uint64_t joinEUI;
    uint64_t devEUI;
    uint8_t appKey[16];
    uint8_t nwkKey[16];

    //TODO: Retrieve Region from Config
    // regional choices: EU868, US915, AU915, AS923, AS923_2, AS923_3, AS923_4, IN865, KR920, CN500
    LoRaWANBand_t Region = AS923_2;
    uint8_t subBand = 0;  // For US915 and AU915, change this to 2, otherwise leave on 0

    //TODO: does it make sense to set fport via config?
    uint8_t mFPort = 1;

    // create the LoRaWAN node
    LoRaWANNode node;

    uint8_t mDownlinkPayload[LORA_MAX_RX_PAYLOAD];  // Make sure this fits your plans!
    size_t  mDownlinkSize;         // To hold the actual payload size received
    LoRaWANEvent_t mDownlinkDetails;

    // This MUST fit the loraWAN application setting
    // if not this may cause buffer overruns & memory violations.
    static const size_t C_DOWNLINK_PAYLOAD = 10;

    // Conservative auto-rejoin on explicit NOT_JOINED errors
    static constexpr int64_t C_REJOIN_MIN_INTERVAL_S = 10 * 60; // 10 minutes
    int64_t mLastRejoinAttemptS = -C_REJOIN_MIN_INTERVAL_S;

    void printDecodedPayload(const uint8_t* payload, size_t  payloadSize);

    esp_err_t init();
    ElocLora(/* args */);

    void errMsg(const __FlashStringHelper* message, int state);
    esp_err_t getRegionFromConfig();

    void calcDevEUIfromMAC();

    esp_err_t sendStatusUpdateMessage();
    esp_err_t sendEventMessage();
    esp_err_t sendIntruderAlarmMessage();
    esp_err_t parseResponse(int16_t state);

    // Intruder alarm uplink scheduling (see ElocLoraLoop)
    bool mIntruderAlarmActive = false;       // edge detection on ElocSystem's intruder flag
    int64_t mNextIntruderUplinkS = 0;        // epoch time the next alarm uplink is due
    static constexpr int64_t C_INTRUDER_RETRY_S = 60;         // retry delay after a failed alarm uplink
    static constexpr uint32_t C_MIN_INTRUDER_INTERVAL_S = 60; // lower bound for intruderCfg.alarmIntervalS

    // Last known GPS position, pushed in from main.cpp via setGpsInfo(). ElocLora deliberately
    // does not include ElocGPS itself: lib/gps already depends on lib/ElocHardware, and the
    // reverse include would create an LDF dependency cycle that breaks the build.
    struct GpsInfo_t {
        bool hasFix = false;
        double lat = 0.0;
        double lng = 0.0;
        uint32_t fixAgeS = 0;
    };
    GpsInfo_t mGpsInfo;
    portMUX_TYPE mGpsInfoMux = portMUX_INITIALIZER_UNLOCKED;

    // Conservative recovery helpers
    bool attemptRejoin(const char* reason);
    bool shouldAttemptRejoin() const;
    int16_t sendReceiveWithRecovery(const uint8_t* uplinkPayload,
                                    size_t uplinkSize,
                                    LoRaWANEvent_t* uplinkDetails);

    // Audio feedback for join events
    void playJoinFeedback(bool success);

    // Signal quality tracking
    float mLastRSSI = 0.0f;      // Last known RSSI in dBm
    float mLastSNR = 0.0f;       // Last known SNR in dB
    bool  mHasRSSI = false;      // Whether we have a valid RSSI reading
    int64_t mLastRSSITimeS = 0;  // Epoch time of last RSSI reading

    /// @brief Store the current radio RSSI/SNR values
    void captureSignalQuality();

    // Session persistence functions
    uint32_t calculateCRC32(const uint8_t* data, size_t length);
    bool isValidSession();
    bool saveSessionToRTC();
    bool loadSessionFromRTC();
    bool saveNoncesToNVS();
    bool loadNoncesFromNVS();
    /// @brief Copy the node's current nonces into RTC memory and persist them to NVS.
    ///        Must also be called after FAILED join attempts, so the DevNonce counter
    ///        keeps increasing across reboots (LoRaWAN 1.0.4 requirement).
    bool persistCurrentNonces();

public:
    virtual ~ElocLora();
    static ElocLora& GetInstance() {
        static ElocLora elocLora;
        return elocLora;
    }
    void ElocLoraLoop();

    /// @brief Get the last known RSSI value (from join or downlink)
    float getLastRSSI() const { return mLastRSSI; }
    /// @brief Get the last known SNR value (from join or downlink)
    float getLastSNR() const { return mLastSNR; }
    /// @brief Whether a valid RSSI reading has been captured
    bool  hasSignalInfo() const { return mHasRSSI; }
    /// @brief Whether LoRa is initialized and joined
    bool  isJoined() const { return mInitDone; }

    /// @brief Push the current GPS position (called from main.cpp's GPS management).
    ///        Used by the intruder alarm uplink; safe to call from a different task
    ///        than the LoRa loop.
    void setGpsInfo(bool hasFix, double lat, double lng, uint32_t fixAgeS) {
        portENTER_CRITICAL(&mGpsInfoMux);
        mGpsInfo.hasFix = hasFix;
        mGpsInfo.lat = lat;
        mGpsInfo.lng = lng;
        mGpsInfo.fixAgeS = fixAgeS;
        portEXIT_CRITICAL(&mGpsInfoMux);
    }
};




#endif // ELOCLORA_HPP_
