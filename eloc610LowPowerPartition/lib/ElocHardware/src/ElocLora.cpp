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

#include <stdio.h>
#include <inttypes.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <driver/rtc_io.h>
#include <esp_pm.h>
#include <esp_mac.h>

#include "ElocLoraConfig.h"
#include "ElocLora.hpp"
#include "ElocConfig.hpp"
#include "ElocSystem.hpp"
#include "ElocStatus.hpp"
#include "Battery.hpp"
#include "WAVFileWriter.h"
#include "config.h"
#include "strutils.h"

const char* TAG = "LoraWAN";

static const uint32_t C_MIN_UPLINK_INTERVAL_S = 10;

#include "RadioLib.h"
#include "../../../include/project_config.h"

// copy over the EUI's & keys in to the something that will not compile if incorrectly formatted
uint8_t C_appKey[] = { RADIOLIB_LORAWAN_APP_KEY };
uint8_t C_nwkKey[] = { RADIOLIB_LORAWAN_NWK_KEY };

#define USE_DEVEUI_FROM_NVS



//BUGME: global timeobject should be in ESP23Time.h
extern ESP32Time timeObject;

//BUGME: this should be shared with ElocCommands
#define ENUM_MACRO(name, v0, v1, v2, v3, v4, v5)\
    enum class name { v0, v1, v2, v3, v4, v5};\
    constexpr const char *name##Strings[] = {  #v0, #v1, #v2, #v3, #v4, #v5}; \
    constexpr const char *toString(name value) {  return name##Strings[static_cast<int>(value)]; }

ENUM_MACRO (RecState, recInvalid, recordOff_detectOff, recordOn_detectOff, recordOn_detectOn, recordOff_detectOn, recordOnEvent);


static RecState calcRecordingState() {
    RecState recState = RecState::recInvalid;

    WAVFileWriter::Mode recMode = wav_writer.get_mode();
    ESP_LOGI("COMMANDS", "WavWriterMode = %s(%d), AI = %s", wav_writer.get_mode_str(), wav_writer.get_mode_int(), ai_run_enable ? "ON" : "OFF");
    switch (recMode) {
        case WAVFileWriter::Mode::disabled:
            if (ai_run_enable) {
                recState = RecState::recordOff_detectOn;
            }
            else {
                recState = RecState::recordOff_detectOff;
            }
            break;
        case WAVFileWriter::Mode::continuous:
            if (ai_run_enable) {
                recState = RecState::recordOn_detectOn;
            }
            else {
                recState = RecState::recordOn_detectOff;
            }
            break;
        case WAVFileWriter::Mode::single:
            if (ai_run_enable) {
                recState = RecState::recordOnEvent;
            }
            else {
                recState = RecState::recInvalid;
            }
            break;
    }
    return recState;
}


// joinEUI - previous versions of LoRaWAN called this AppEUI
// for development purposes you can use all zeros - see wiki for details
#define RADIOLIB_LORAWAN_JOIN_EUI  0x0000000000000000

ElocLora::ElocLora(/* args */):
    mInitDone(false),
    loraSPI(VSPI), 
    radio (new Module(PIN_LORA_CS, PIN_LORA_DIO1, PIN_LORA_RST, PIN_LORA_BUSY, loraSPI, loraSpiSettings)),
    // create the LoRaWAN node
    //BUGME: handle subband correct for US915 (need to be 2)
    node(&radio, &Region, subBand)
{
  const loraWAN_keys_t& loraKeys = ElocSystem::GetInstance().getLoraWAN_Keys();
  if (getRegionFromConfig() == ESP_OK) {
    if (getConfig().loraConfig.upLinkIntervalS > C_MIN_UPLINK_INTERVAL_S) {
      uplinkIntervalSeconds = getConfig().loraConfig.upLinkIntervalS;
    }
    else {
      ESP_LOGW(TAG, "Update Interval %d is too short. Minimum %d second allowed", getConfig().loraConfig.upLinkIntervalS, C_MIN_UPLINK_INTERVAL_S);
    }
    calcDevEUIfromMAC();
    ESP_LOGI(TAG, "init & Join Lora with uplink Interval %d seconds\n", uplinkIntervalSeconds);
    //TODO: how to handle the joinEUI... check that
    joinEUI = RADIOLIB_LORAWAN_JOIN_EUI;
#ifdef USE_DEVEUI_FROM_NVS
    devEUI = loraKeys.devEUI;
#endif
    memcpy(this->appKey, loraKeys.appKey, sizeof(appKey));
    memcpy(this->nwkKey, loraKeys.nwkKey, sizeof(nwkKey));
    if (esp_err_t err=init() != ESP_OK) {
        ESP_LOGE(TAG, "init failed with %s! Lora Communication will be unavailable!\n", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "init completed!\n");
  }
  // if Region cannot determined than do nothing with LoraWAN
}

ElocLora::~ElocLora()
{
}

// result code to text - these are error codes that can be raised when using LoRaWAN
// however, RadioLib has many more - see https://jgromes.github.io/RadioLib/group__status__codes.html for a complete list
String stateDecode(const int16_t result) {
  switch (result) {
  case RADIOLIB_ERR_NONE:
    return "ERR_NONE";
  case RADIOLIB_ERR_CHIP_NOT_FOUND:
    return "ERR_CHIP_NOT_FOUND";
  case RADIOLIB_ERR_PACKET_TOO_LONG:
    return "ERR_PACKET_TOO_LONG";
  case RADIOLIB_ERR_RX_TIMEOUT:
    return "ERR_RX_TIMEOUT";
  case RADIOLIB_ERR_CRC_MISMATCH:
    return "ERR_CRC_MISMATCH";
  case RADIOLIB_ERR_INVALID_BANDWIDTH:
    return "ERR_INVALID_BANDWIDTH";
  case RADIOLIB_ERR_INVALID_SPREADING_FACTOR:
    return "ERR_INVALID_SPREADING_FACTOR";
  case RADIOLIB_ERR_INVALID_CODING_RATE:
    return "ERR_INVALID_CODING_RATE";
  case RADIOLIB_ERR_INVALID_FREQUENCY:
    return "ERR_INVALID_FREQUENCY";
  case RADIOLIB_ERR_INVALID_OUTPUT_POWER:
    return "ERR_INVALID_OUTPUT_POWER";
  case RADIOLIB_ERR_NETWORK_NOT_JOINED:
	  return "RADIOLIB_ERR_NETWORK_NOT_JOINED";
  case RADIOLIB_ERR_DOWNLINK_MALFORMED:
    return "RADIOLIB_ERR_DOWNLINK_MALFORMED";
  case RADIOLIB_ERR_INVALID_REVISION:
    return "RADIOLIB_ERR_INVALID_REVISION";
  case RADIOLIB_ERR_INVALID_PORT:
    return "RADIOLIB_ERR_INVALID_PORT";
  case RADIOLIB_ERR_NO_RX_WINDOW:
    return "RADIOLIB_ERR_NO_RX_WINDOW";
  case RADIOLIB_ERR_INVALID_CID:
    return "RADIOLIB_ERR_INVALID_CID";
  case RADIOLIB_ERR_UPLINK_UNAVAILABLE:
    return "RADIOLIB_ERR_UPLINK_UNAVAILABLE";
  case RADIOLIB_ERR_COMMAND_QUEUE_FULL:
    return "RADIOLIB_ERR_COMMAND_QUEUE_FULL";
  case RADIOLIB_ERR_COMMAND_QUEUE_ITEM_NOT_FOUND:
    return "RADIOLIB_ERR_COMMAND_QUEUE_ITEM_NOT_FOUND";
  case RADIOLIB_ERR_JOIN_NONCE_INVALID:
    return "RADIOLIB_ERR_JOIN_NONCE_INVALID";
  case RADIOLIB_ERR_N_FCNT_DOWN_INVALID:
    return "RADIOLIB_ERR_N_FCNT_DOWN_INVALID";
  case RADIOLIB_ERR_A_FCNT_DOWN_INVALID:
    return "RADIOLIB_ERR_A_FCNT_DOWN_INVALID";
  case RADIOLIB_ERR_DWELL_TIME_EXCEEDED:
    return "RADIOLIB_ERR_DWELL_TIME_EXCEEDED";
  case RADIOLIB_ERR_CHECKSUM_MISMATCH:
    return "RADIOLIB_ERR_CHECKSUM_MISMATCH";
  case RADIOLIB_ERR_NO_JOIN_ACCEPT:
    return "RADIOLIB_ERR_NO_JOIN_ACCEPT";
  case RADIOLIB_LORAWAN_SESSION_RESTORED:
    return "RADIOLIB_LORAWAN_SESSION_RESTORED";
  case RADIOLIB_LORAWAN_NEW_SESSION:
    return "RADIOLIB_LORAWAN_NEW_SESSION";
  case RADIOLIB_ERR_NONCES_DISCARDED:
    return "RADIOLIB_ERR_NONCES_DISCARDED";
  case RADIOLIB_ERR_SESSION_DISCARDED:
    return "RADIOLIB_ERR_SESSION_DISCARDED";
  }
  return "See https://jgromes.github.io/RadioLib/group__status__codes.html";
}

esp_err_t ElocLora::getRegionFromConfig() {
  const String& cfgRegion = getConfig().loraConfig.loraRegion;
  if (cfgRegion == "EU868") {
    Region= EU868;
  }
  else if (cfgRegion == "US915") {
    Region= US915;
  }
  else if (cfgRegion == "AU915") {
    Region= AU915;
  }
  else if (cfgRegion == "AS923") {
    Region= AS923;
  }
  else if (cfgRegion == "AS923_2") {
    Region= AS923_2;
  }
  else if (cfgRegion == "AS923_3") {
    Region= AS923_3;
  }
  else if (cfgRegion == "AS923_4") {
    Region= AS923_4;
  }
  else if (cfgRegion == "IN865") {
    Region= IN865;
  }
  else if (cfgRegion == "KR920") {
    Region= KR920;
  }
  else if (cfgRegion == "CN500") {
    Region= CN500;
  }
  else {
    ESP_LOGE(TAG, "Invalid Lora Region Config: %s", cfgRegion.c_str());
    return ESP_ERR_INVALID_ARG;
  }
  ESP_LOGI(TAG, "Setting Lora Region Config: %s", cfgRegion.c_str());
  return ESP_OK;
}

void ElocLora::calcDevEUIfromMAC() {
  uint8_t mac[6];
  uint64_t devEUI64 = 0;
  uint8_t* eui64 = reinterpret_cast<uint8_t*>(&devEUI64); // helper for stitching together the bytes of the MAC address

  esp_read_mac(mac, ESP_MAC_WIFI_STA);

  ESP_LOGI(TAG, "ESP32 Base MAC address = %s", array_to_HexString(mac, sizeof(mac)).c_str());
  // take endianess into account when converting to uint64_t
  eui64[7] = mac[0];
  eui64[6] = mac[1];
  eui64[5] = mac[2];
  eui64[4] = 0xFF;
  eui64[3] = 0xFE;
  eui64[2] = mac[3];
  eui64[1] = mac[4];
  eui64[0] = mac[5];
  ESP_LOGI(TAG, "MAC address based devEUI = %llX", devEUI64);

  this->devEUI = devEUI64;
}


#include "esp_log.h"
// helper function to display any issues
void debug(bool failed, const __FlashStringHelper* message, int state, bool halt) {
  if(failed) {
    String msg = message;
    msg += " - ";
    msg += stateDecode(state);
    msg += " (";
    msg += state;
    msg += ")";
    ESP_LOGE("RADIOLIB_DBG", "%s", msg.c_str());
    while(halt) { delay(1); }
  }
}

// helper function to display a byte array
void arrayDump(uint8_t *buffer, uint16_t len) {
  for(uint16_t c = 0; c < len; c++) {
    char b = buffer[c];
    if(b < 0x10) { Serial.print('0'); }
    Serial.print(b, HEX);
  }
  Serial.println();
}


void ElocLora::printDecodedPayload(const uint8_t* payload, size_t  payloadSize) {
  const char* colors[] = {"red", "green", "blue"};

  if (payloadSize != 3) {
    ESP_LOGE(TAG, "Received payload does not match (!=3) %u", payloadSize);
    return;
  }
  if (payload[0] >= sizeof(colors)) {
    ESP_LOGE(TAG, "Received payload color index out of range: %d", payload[0]);
  }
  else {

  }
  ESP_LOGI(TAG, "Received Payload: color = %s", colors[payload[0]]);

  uint16_t counter = payload[2] << 8 | payload[1];
  ESP_LOGI(TAG, "Received Payload: counter = %u", counter);
}

void ElocLora::errMsg(const __FlashStringHelper* message, int state) {
  String msg = message;
  msg += " - ";
  msg += stateDecode(state);
  msg += " (";
  msg += state;
  msg += ")";
  ESP_LOGE(TAG, "%s", msg.c_str());
}

esp_err_t ElocLora::init() {
  //BUGME: handle serial somewhere else? or use ESP IDF based HAL instead of Arduino
  Serial.begin(115200);
  while(!Serial);
  if (!getConfig().loraConfig.loraEnable) {
    ESP_LOGW(TAG, "Lora is not enabled... skipping Initialization");
    return ESP_OK;
  }

  ESP_LOGI(TAG, "MOSI: %u", PIN_LORA_MOSI);
  ESP_LOGI(TAG, "MISO: %u", PIN_LORA_MISO);
  ESP_LOGI(TAG, "SCK: %u", PIN_LORA_CLK);
  ESP_LOGI(TAG, "SS: %u", PIN_LORA_CS);  
  delay(5000);  // Give time to switch to the serial monitor
  ESP_LOGI(TAG, "Setup ... ");

  loraSPI.begin(PIN_LORA_CLK, PIN_LORA_MISO, PIN_LORA_MOSI, PIN_LORA_CS);

  ESP_LOGI(TAG, "Initialise the radio");
  radio.XTAL = true; // do not use TCXO for RA01 modules
  int16_t state = radio.begin();
  if (state != RADIOLIB_ERR_NONE) {
    this->errMsg(F("Initialise radio failed"), state);
    return ESP_ERR_NOT_FINISHED;
  }
  //BUGME (CRITICAL): Remove this print as it reveals secret key information.
  ESP_LOGI(TAG, "LoraWAN Data: devEUI=0x%llX, appKey = %s, nwkKey = %s",
      this->devEUI, 
      array_to_HexString(this->appKey, sizeof(this->appKey)).c_str(), 
      array_to_HexString(this->nwkKey, sizeof(this->nwkKey)).c_str());
  // Setup the OTAA session information
  state = node.beginOTAA(joinEUI, devEUI, nwkKey, appKey);
  if (state != RADIOLIB_ERR_NONE) {
    this->errMsg(F("Initialise node failed"), state);
    return ESP_ERR_NOT_FINISHED;
  }

  ESP_LOGI(TAG, "Join ('login') the LoRaWAN Network");
  state = node.activateOTAA();
  if (state != RADIOLIB_LORAWAN_NEW_SESSION) {
    this->errMsg(F("Join failed"), state);
    return ESP_ERR_NOT_FINISHED;
  }

  ESP_LOGI(TAG, "Ready!\n");
  mInitDone = true;
  return ESP_OK;
}

void ElocLora::ElocLoraLoop() {

    if ((!mInitDone) || (!getConfig().loraConfig.loraEnable)) {
      // if initialization failed Lora is not available so we skip everything
      return;
    }
    static int64_t lastLoraUplinkTimeS = 0;
    int64_t timeDiff = esp_timer_get_time()/1000/1000 - lastLoraUplinkTimeS;
    if  (timeDiff >= uplinkIntervalSeconds) {
      ESP_LOGI(TAG, "Sending uplink");  
      lastLoraUplinkTimeS = esp_timer_get_time()/1000/1000;
      
      sendStatusUpdateMessage();

      ESP_LOGI(TAG, "Next uplink in %d seconds\n", uplinkIntervalSeconds);
    }
}

esp_err_t ElocLora::sendStatusUpdateMessage() {

    // you can also retrieve additional information about an uplink or
    // downlink by passing a reference to LoRaWANEvent_t structure
    LoRaWANEvent_t uplinkDetails;
    // This is the place to gather the sensor inputs
    // Instead of reading any real sensor, we just generate some random numbers as example
    uint8_t value1 = radio.random(100);
    uint16_t value2 = radio.random(2000);

    // Build payload byte array
    int64_t time = timeObject.getEpoch();

    uint8_t uplinkPayload[LORA_MAX_TX_PAYLOAD];
    uint8_t idx = 0;
    uint8_t msgType = t_LoraMsgType::STATUS_MSG;
    if (idx < LORA_MAX_TX_PAYLOAD) uplinkPayload[idx++] = (msgType << 4) | (LORA_MSG_VERS & 0x0F);
    for (int i = sizeof(time) -1; i >= 0; i--) {
      if (idx < LORA_MAX_TX_PAYLOAD) uplinkPayload[idx++] = (time >> (i*8))  & 0xFF; // See notes for high/lowByte functions
    }
    uint8_t batSoC = static_cast<uint8_t>(Battery::GetInstance().getSoC());
    if (idx < LORA_MAX_TX_PAYLOAD) uplinkPayload[idx++] = batSoC;
    if (idx < LORA_MAX_TX_PAYLOAD) uplinkPayload[idx++] = static_cast<int>(calcRecordingState());
    
    ESP_LOGI(TAG, "Sending data (%d bytes): type = %d, time = %lld, SoC=%hu", idx, msgType, time, batSoC);
    arrayDump(uplinkPayload, idx);
    // Perform an uplink
    int16_t state = node.sendReceive(uplinkPayload, idx, mFPort, mDownlinkPayload, &mDownlinkSize, false, &uplinkDetails,
                                     &mDownlinkDetails);
    debug(state < RADIOLIB_ERR_NONE, F("Error in sendReceive"), state, false);

    if (state < RADIOLIB_ERR_NONE) {
      return ESP_FAIL;
    }
    return parseResponse(state);
}
esp_err_t ElocLora::sendEventMessage() {
  

    // you can also retrieve additional information about an uplink or
    // downlink by passing a reference to LoRaWANEvent_t structure
    LoRaWANEvent_t uplinkDetails;
    // This is the place to gather the sensor inputs
    // Instead of reading any real sensor, we just generate some random numbers as example
    uint8_t value1 = radio.random(100);
    uint16_t value2 = radio.random(2000);

    // Build payload byte array
    int64_t time = timeObject.getEpoch();

    uint8_t uplinkPayload[LORA_MAX_TX_PAYLOAD];
    uint8_t idx = 0;

    if (idx < LORA_MAX_TX_PAYLOAD) uplinkPayload[idx++] = (t_LoraMsgType::EVENT_MSG << 4) | (LORA_MSG_VERS & 0x0F);
    for (int i = sizeof(time) -1; i < 0; i--) {
      if (idx < LORA_MAX_TX_PAYLOAD) uplinkPayload[idx++] = (time >> (i))  & 0xFF; // See notes for high/lowByte functions
    }
    // TODO: Add AI Detected event here

    ESP_LOGI(TAG, "Sending data: type = %d, time = %lld", t_LoraMsgType::STATUS_MSG, time);
    // Perform an uplink
    int16_t state = node.sendReceive(uplinkPayload, sizeof(uplinkPayload), mFPort, mDownlinkPayload, &mDownlinkSize, false, &uplinkDetails,
                                     &mDownlinkDetails);
    debug(state < RADIOLIB_ERR_NONE, F("Error in sendReceive"), state, false);

    if (state < RADIOLIB_ERR_NONE) {
      return ESP_FAIL;
    }
    return parseResponse(state);
}

esp_err_t ElocLora::parseResponse(int16_t state) {

    // Check if a downlink was received
    // (state 0 = no downlink, state 1/2 = downlink in window Rx1/Rx2)
    if (state > 0) {
        ESP_LOGI(TAG, "Received a downlink");

        // Did we get a downlink with data for us
        if (mDownlinkSize > 0) {
            ESP_LOGI(TAG, "Downlink data: ");
            arrayDump(mDownlinkPayload, mDownlinkSize);
            printDecodedPayload(mDownlinkPayload, mDownlinkSize);
        } else {
            ESP_LOGI(TAG, "<MAC commands only>");
        }

        // print RSSI (Received Signal Strength Indicator)
        ESP_LOGI(TAG, "[LoRaWAN] RSSI:\t\t%f dBm", radio.getRSSI());

        // print SNR (Signal-to-Noise Ratio)
        ESP_LOGI(TAG, "[LoRaWAN] SNR:\t\t%f dB", radio.getSNR());

        // print extra information about the event
        ESP_LOGI(TAG, "[LoRaWAN] Event information:");
        ESP_LOGI(TAG, "[LoRaWAN] Confirmed:\t %s", mDownlinkDetails.confirmed ? "true" : "false");
        ESP_LOGI(TAG, "[LoRaWAN] Confirming:\t %s", mDownlinkDetails.confirming ? "true" : "false");
        ESP_LOGI(TAG, "[LoRaWAN] Datarate:\t %d", mDownlinkDetails.datarate);
        ESP_LOGI(TAG, "[LoRaWAN] Frequency:\t %.3f MHz", mDownlinkDetails.freq);
        ESP_LOGI(TAG, "[LoRaWAN] Frame count:\t %d", mDownlinkDetails.fCnt);
        ESP_LOGI(TAG, "[LoRaWAN] Port:\t\t %d", mDownlinkDetails.fPort);
        ESP_LOGI(TAG, "[LoRaWAN] Time-on-air: \t %ld ms", node.getLastToA());
        ESP_LOGI(TAG, "[LoRaWAN] Rx window: \t %d", state);
    } else {
        ESP_LOGI(TAG, "No downlink received");
    }
    return ESP_OK;
}
