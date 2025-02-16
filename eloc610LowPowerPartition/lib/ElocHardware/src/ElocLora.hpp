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

class ElocLora
{
private:
    enum t_LoraMsgType {
        STATUS_MSG = 0,
        EVENT_MSG = 1,
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
    uint32_t uplinkIntervalSeconds = 1UL * 60UL;    // minutes x seconds

    uint64_t joinEUI;
    uint64_t devEUI;
    uint8_t appKey[16];
    uint8_t nwkKey[16];

    //TODO: Retrieve Region from Config
    // regional choices: EU868, US915, AU915, AS923, AS923_2, AS923_3, AS923_4, IN865, KR920, CN500
    LoRaWANBand_t Region = EU868;
    uint8_t subBand = 0;  // For US915, change this to 2, otherwise leave on 0

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

    void printDecodedPayload(const uint8_t* payload, size_t  payloadSize);

    esp_err_t init();
    ElocLora(/* args */);

    void errMsg(const __FlashStringHelper* message, int state);
    esp_err_t getRegionFromConfig();

    void calcDevEUIfromMAC();

    esp_err_t sendStatusUpdateMessage();
    esp_err_t sendEventMessage();
    esp_err_t parseResponse(int16_t state);

public:
    virtual ~ElocLora();
    static ElocLora& GetInstance() {
        static ElocLora elocLora;
        return elocLora;
    }
    void ElocLoraLoop();
};




#endif // ELOCLORA_HPP_