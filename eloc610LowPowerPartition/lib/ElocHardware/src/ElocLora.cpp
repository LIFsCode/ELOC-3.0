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

#include "ElocLoraConfig.h"
#include "ElocLora.hpp"
#include <stdio.h>
#include <inttypes.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <driver/rtc_io.h>
#include <esp_pm.h>

#include "config.h"

const char* TAG = "LoraWAN";


void printDecodedPayload(const uint8_t* payload, size_t  payloadSize) {
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

void ElocLoraSetup() {
  Serial.begin(115200);
  while(!Serial);
  ESP_LOGI(TAG, "MOSI: %u", PIN_LORA_MOSI);
  ESP_LOGI(TAG, "MISO: %u", PIN_LORA_MISO);
  ESP_LOGI(TAG, "SCK: %u", PIN_LORA_CLK);
  ESP_LOGI(TAG, "SS: %u", PIN_LORA_CS);  
  delay(5000);  // Give time to switch to the serial monitor
  ESP_LOGI(TAG, "\nSetup ... ");

  loraSPI.begin(PIN_LORA_CLK, PIN_LORA_MISO, PIN_LORA_MOSI, PIN_LORA_CS);

  ESP_LOGI(TAG, "Initialise the radio");
  radio.XTAL = true; // do not use TCXO for RA01 modules
  int16_t state = radio.begin();
  debug(state != RADIOLIB_ERR_NONE, F("Initialise radio failed"), state, true);

  // Setup the OTAA session information
  state = node.beginOTAA(joinEUI, devEUI, nwkKey, appKey);
  debug(state != RADIOLIB_ERR_NONE, F("Initialise node failed"), state, true);

  ESP_LOGI(TAG, "Join ('login') the LoRaWAN Network");
  state = node.activateOTAA();
  debug(state != RADIOLIB_LORAWAN_NEW_SESSION, F("Join failed"), state, true);

  ESP_LOGI(TAG, "Ready!\n");
}

void ElocLoraLoop() {

    
  static int64_t lastLoraUplinkTimeS = 0;
  int64_t timeDiff = esp_timer_get_time()/1000/1000 - lastLoraUplinkTimeS;
  if  (timeDiff >= uplinkIntervalSeconds) {
    ESP_LOGI(TAG, "Sending uplink");  
    lastLoraUplinkTimeS = esp_timer_get_time()/1000/1000;
    

    uint8_t downlinkPayload[10];  // Make sure this fits your plans!
    size_t  downlinkSize;         // To hold the actual payload size received

    // you can also retrieve additional information about an uplink or 
    // downlink by passing a reference to LoRaWANEvent_t structure
    LoRaWANEvent_t uplinkDetails;
    LoRaWANEvent_t downlinkDetails;
    
    uint8_t fPort = 1;

    // This is the place to gather the sensor inputs
    // Instead of reading any real sensor, we just generate some random numbers as example
    uint8_t value1 = radio.random(100);
    uint16_t value2 = radio.random(2000);

    // Build payload byte array
    uint8_t uplinkPayload[3];
    uplinkPayload[0] = value1;
    uplinkPayload[1] = highByte(value2);   // See notes for high/lowByte functions
    uplinkPayload[2] = lowByte(value2);
    
    ESP_LOGI(TAG, "Sending data: val1 = %d, val2 = %d", value1, value2);
    // Perform an uplink
    int16_t state = node.sendReceive(uplinkPayload, sizeof(uplinkPayload), fPort, downlinkPayload, &downlinkSize, false, &uplinkDetails, &downlinkDetails);    
    debug(state < RADIOLIB_ERR_NONE, F("Error in sendReceive"), state, false);

    // Check if a downlink was received 
    // (state 0 = no downlink, state 1/2 = downlink in window Rx1/Rx2)
    if(state > 0) {
      ESP_LOGI(TAG, "Received a downlink");    
      
      // Did we get a downlink with data for us
      if(downlinkSize > 0) {
        ESP_LOGI(TAG, "Downlink data: ");
        arrayDump(downlinkPayload, downlinkSize);
        printDecodedPayload(downlinkPayload, downlinkSize);
      } else {
        ESP_LOGI(TAG, "<MAC commands only>");
      }

      // print RSSI (Received Signal Strength Indicator)
      ESP_LOGI(TAG, "[LoRaWAN] RSSI:\t\t%f dBm", radio.getRSSI());

      // print SNR (Signal-to-Noise Ratio)
      ESP_LOGI(TAG, "[LoRaWAN] SNR:\t\t%f dB", radio.getSNR());

      // print extra information about the event
      ESP_LOGI(TAG, "[LoRaWAN] Event information:");
      ESP_LOGI(TAG, "[LoRaWAN] Confirmed:\t %s", downlinkDetails.confirmed ? "true" : "false");
      ESP_LOGI(TAG, "[LoRaWAN] Confirming:\t %s", downlinkDetails.confirming ? "true" : "false");
      ESP_LOGI(TAG, "[LoRaWAN] Datarate:\t %d", downlinkDetails.datarate);
      ESP_LOGI(TAG, "[LoRaWAN] Frequency:\t %.3f MHz", downlinkDetails.freq);
      ESP_LOGI(TAG, "[LoRaWAN] Frame count:\t %d", downlinkDetails.fCnt);
      ESP_LOGI(TAG, "[LoRaWAN] Port:\t\t %d", downlinkDetails.fPort);
      ESP_LOGI(TAG, "[LoRaWAN] Time-on-air: \t %ld ms", node.getLastToA());
      ESP_LOGI(TAG, "[LoRaWAN] Rx window: \t %d", state);
    } else {
      ESP_LOGI(TAG, "No downlink received");
    }

    ESP_LOGI(TAG, "Next uplink in %d seconds\n", uplinkIntervalSeconds);
  }
}


