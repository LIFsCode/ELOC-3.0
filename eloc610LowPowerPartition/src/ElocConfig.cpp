/*
 * Created on Wed Apr 26 2023
 *
 * Project: International Elephant Project (Wildlife Conservation International)
 *
 * The MIT License (MIT)
 * Copyright (c) 2023 Fabian Lindner
 * based on the work of @tbgilson (https://github.com/tbgilson)
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
#include <esp_log.h>

#include "Esp.h"
#include <FS.h>
#include "SPIFFS.h"

#include "config.h"
#include "ElocConfig.hpp"

const char* TAG = "CONFIG";

//BUGME: encapsulate these in a struct & implement a getter
micInfo_t gMicInfo {
    .MicType="ns",
    .MicBitShift="11",
    .MicGPSCoords="ns",
    .MicPointingDirectionDegrees="ns",
    .MicHeight="ns",
    .MicMountType="ns",
    .MicBluetoothOnOrOff="on"
};

void setMicBitShift(String MicBitShift) {
    gMicInfo.MicBitShift = MicBitShift;
}
void setMicType(String MicType) {
    gMicInfo.MicType = MicType;
}
void setMicBluetoothOnOrOff(String MicBluetoothOnOrOff) {
    gMicInfo.MicBluetoothOnOrOff = MicBluetoothOnOrOff;
}

const micInfo_t& getMicInfo() {
    return gMicInfo;
}
void writeMicInfo() {

    File file2 = SPIFFS.open("/micinfo.txt", FILE_WRITE);

    file2.print(gMicInfo.MicType + '\n');
    file2.print(gMicInfo.MicBitShift + '\n');
    file2.print(gMicInfo.MicGPSCoords + '\n');
    file2.print(gMicInfo.MicPointingDirectionDegrees + '\n');
    file2.print(gMicInfo.MicHeight + '\n');
    file2.print(gMicInfo.MicMountType + '\n');
    file2.print(gMicInfo.MicBluetoothOnOrOff + '\n');
    file2.close();
    String info = "micinfo: " + gMicInfo.MicType + "  " + gMicInfo.MicBitShift + "  " + gMicInfo.MicGPSCoords + "  " + gMicInfo.MicPointingDirectionDegrees + " " + gMicInfo.MicHeight + " " + gMicInfo.MicMountType + " " + gMicInfo.MicBluetoothOnOrOff;
    ESP_LOGI(TAG, "%s", info.c_str());
}

void readMicInfo() {

    if (!(SPIFFS.exists("/micinfo.txt"))) {
        printf("micinfo.txt not exist");
        writeMicInfo();
    }
    File file2 = SPIFFS.open("/micinfo.txt", FILE_READ);
    gMicInfo.MicType = file2.readStringUntil('\n');
    gMicInfo.MicType.trim();
    gMicInfo.MicBitShift = file2.readStringUntil('\n');
    gMicInfo.MicBitShift.trim();
    gMicInfo.MicGPSCoords = file2.readStringUntil('\n');
    gMicInfo.MicGPSCoords.trim();
    gMicInfo.MicPointingDirectionDegrees = file2.readStringUntil('\n');
    gMicInfo.MicPointingDirectionDegrees.trim();
    gMicInfo.MicHeight = file2.readStringUntil('\n');
    gMicInfo.MicHeight.trim();
    gMicInfo.MicMountType = file2.readStringUntil('\n');
    gMicInfo.MicMountType.trim();
    gMicInfo.MicBluetoothOnOrOff = file2.readStringUntil('\n');
    gMicInfo.MicBluetoothOnOrOff.trim();

    file2.close();
    String info = "micinfo: " + gMicInfo.MicType + "  " + gMicInfo.MicBitShift + "  " + gMicInfo.MicGPSCoords + "  " + gMicInfo.MicPointingDirectionDegrees + " " + gMicInfo.MicHeight + " " + gMicInfo.MicMountType + " " + gMicInfo.MicBluetoothOnOrOff;
    ESP_LOGI(TAG, "%s", info.c_str());
}
/**************************************************************************************************/


/*************************** Global settings via BT Config ****************************************/
//BUGME: encapsulate these in a struct & implement a getter
uint32_t gSampleRate = I2S_DEFAULT_SAMPLE_RATE;
int gSecondsPerFile= 60;
String gLocation = "not_set";
String gSyncPhoneOrGoogle; //will be either G or P (google or phone).
String gLocationCode="unknown";
String gLocationAccuracy="99";
long gLastSystemTimeUpdate; // local system time of last time update PLUS minutes since last phone update 


elocConfig_T gElocConfig {
    .sampleRate = I2S_DEFAULT_SAMPLE_RATE,
    .useAPLL = true,
    .secondsPerFile = 60,
    .listenOnly = false,
    // Power management
    .cpuMaxFrequencyMHZ = 80,    // minimum 80
    .cpuMinFrequencyMHZ = 10,
    .cpuEnableLightSleep = true,
    .bluetoothEnableAtStart = false,
    .bluetoothEnableOnTapping = true,
    .testI2SClockInput = false
};
const elocConfig_T& getConfig() {
    return gElocConfig;
}

/**************************************************************************************************/

String readNodeName() {

    // int a =gDeleteMe.length();
    // if (a==2) {a=1;}
    if (!(SPIFFS.exists("/nodename.txt"))) {

        ESP_LOGI(TAG, "No nodename set. Returning ELOC_NONAME");
        return ("ELOC_NONAME");
    }

    File file2 = SPIFFS.open("/nodename.txt", FILE_READ);

    // String temp = file2.readStringUntil('\n');

    String temp = file2.readString();
    temp.trim();
    file2.close();
    ESP_LOGI(TAG, "node name: %s", temp.c_str());
    return (temp);
    // return("");
}

/*************************** Global settings via config file **************************************/
//BUGME: handle this through file system
extern bool gMountedSDCard;

//BUGME: encapsulate these in a struct & implement a getter
bool gTimingFix=false;

void readConfig() {

    char line[128] = "";
    char *position;
    if (gMountedSDCard) {
        FILE *f = fopen("/sdcard/eloctest.txt", "r");
        if (f == NULL) {
            ESP_LOGI(TAG, "no eloc testing file on root of SDCARD ");
            // return;
        } else {
            ESP_LOGI(TAG, "\n\n\n");
            while (!feof(f)) {
                fgets(line, sizeof(line), f);
                // ESP_LOGI(TAG, "%s", line);

                position = strstr(line, "SampleRate:");
                if (position != NULL) {
                    position = strstr(line, ":");
                    gSampleRate = atoi(position + 1);
                    ESP_LOGI(TAG, "sample rate override %u", gSampleRate);
                }

                position = strstr(line, "MaxFrequencyMHZ:");
                if (position != NULL) {
                    position = strstr(line, ":");
                    gElocConfig.cpuMaxFrequencyMHZ = atoi(position + 1);
                    ESP_LOGI(TAG, "Max Frequency MHZ override %d", gElocConfig.cpuMaxFrequencyMHZ);
                }
                position = strstr(line, "MinFrequencyMHZ:");
                if (position != NULL) {
                    position = strstr(line, ":");
                    gElocConfig.cpuMinFrequencyMHZ = atoi(position + 1);
                    ESP_LOGI(TAG, "Min Frequency MHZ override %d", gElocConfig.cpuMinFrequencyMHZ);
                }

                position = strstr(line, "gain:");
                if (position != NULL) {
                    position = strstr(line, ":");
                    gMicInfo.MicBitShift = String(position + 1);
                    ESP_LOGI(TAG, "gain override: %d", (int)gMicInfo.MicBitShift.toInt());
                }

                position = strstr(line, "SecondsPerFile:");
                if (position != NULL) {
                    position = strstr(line, ":");
                    gSecondsPerFile = atoi(position + 1);
                    ESP_LOGI(TAG, "Seconds per File override: %d", gSecondsPerFile);
                }

                position = strstr(line, "UseAPLL:no");
                if (position != NULL) {
                    gElocConfig.useAPLL = false;
                }
                position = strstr(line, "UseAPLL:yes");
                if (position != NULL) {
                    gElocConfig.useAPLL = true;
                }
                position = strstr(line, "TryLightSleep?:yes");
                if (position != NULL) {
                    gElocConfig.cpuEnableLightSleep = true;
                }
                position = strstr(line, "TryLightSleep?:no");
                if (position != NULL) {
                    gElocConfig.cpuEnableLightSleep = false;
                }
                position = strstr(line, "ListenOnly?:no");
                if (position != NULL) {
                    gElocConfig.listenOnly = false;
                }
                position = strstr(line, "ListenOnly?:yes");
                if (position != NULL) {
                    gElocConfig.listenOnly = true;
                }
                position = strstr(line, "TimingFix:no");
                if (position != NULL) {
                    gTimingFix = false;
                }
                position = strstr(line, "TimingFix:yes");
                if (position != NULL) {
                    gTimingFix = true;
                }
                position = strstr(line, "gElocConfig.testI2SClockInput:no");
                if (position != NULL) {
                    gElocConfig.testI2SClockInput = false;
                }
                position = strstr(line, "gElocConfig.testI2SClockInput:yes");
                if (position != NULL) {
                    gElocConfig.testI2SClockInput = true;
                }
            }
            ESP_LOGI(TAG, "Use APLL override: %d", gElocConfig.useAPLL);
            ESP_LOGI(TAG, "Enable light Sleep override: %d", gElocConfig.cpuEnableLightSleep);
            ESP_LOGI(TAG, "Listen Only override: %d", gElocConfig.listenOnly);
            ESP_LOGI(TAG, "Timing fix override: %d", gTimingFix);
            ESP_LOGI(TAG, "Test i2sClockInput: %d", gElocConfig.testI2SClockInput);
            ESP_LOGI(TAG, "\n\n\n");

            fclose(f);
        }
    }

    i2s_mic_Config.sample_rate = gSampleRate;
    if (gSampleRate <= 32000) { // my wav files sound wierd if apll clock raate is > 32kh. So force non-apll clock if >32khz
        i2s_mic_Config.use_apll = gElocConfig.useAPLL;
        ESP_LOGI(TAG, "Sample Rate is < 32khz USE APLL Clock %d", gElocConfig.useAPLL);
    } else {
        i2s_mic_Config.use_apll = false;
        ESP_LOGI(TAG, "Sample Rate is > 32khz Forcing NO_APLL ");
    }
}
