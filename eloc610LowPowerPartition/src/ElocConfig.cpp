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
    Serial.println("micinfo: " + gMicInfo.MicType + "  " + gMicInfo.MicBitShift + "  " + gMicInfo.MicGPSCoords + "  " + gMicInfo.MicPointingDirectionDegrees + " " + gMicInfo.MicHeight + " " + gMicInfo.MicMountType + " " + gMicInfo.MicBluetoothOnOrOff);
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
    Serial.println("micinfo: " + gMicInfo.MicType + "  " + gMicInfo.MicBitShift + "  " + gMicInfo.MicGPSCoords + "  " + gMicInfo.MicPointingDirectionDegrees + " " + gMicInfo.MicHeight + " " + gMicInfo.MicMountType + " " + gMicInfo.MicBluetoothOnOrOff);
}
/**************************************************************************************************/


/*************************** Global settings via BT Config ****************************************/
//BUGME: encapsulate these in a struct & implement a getter
uint32_t gSampleRate;
int gSecondsPerFile= 60;
String gLocation = "not_set";

//BUGME: these function should be part of some bluetooth handler
//       settings should only return settings as string, not directly send it 
void btwrite(String theString);
void sendElocStatus();

void sendSettings() {

    btwrite("#" + String(gSampleRate) + "#" + String(gSecondsPerFile) + "#" +
            gLocation);
    vTaskDelay(pdMS_TO_TICKS(100));
    // btwrite("elocName: "+readNodeName() + " "+gFirmwareVersion);
    vTaskDelay(pdMS_TO_TICKS(100));
    // btwrite(String(gFreeSpace)+ " GB free");
}

void writeSettings(String settings) {

    settings.trim();

    if (settings.endsWith("getstats")) {
        btwrite("\n\n");
        sendElocStatus();
        btwrite("\n\n");
        delay(500);
        sendSettings();
        return;
    }

    if (settings.endsWith("vcal")) {
        ESP_LOGW(TAG, "vcal is not supported! Usefullness is questionable");
        /*********** vcal is not supported currently ***********
         *  If reintroduced it must be calibrated against a defined value, e.g. by measuring with a external multimeter

        btwrite("\n\nCalibrating voltage with VLow="+String(gvLow)+ " volts\n");
        calculateVoltageOffset();
         btwrite("voltage offset is now "+String(gVoltageOffset));

        File file = SPIFFS.open("/voltageoffset.txt", FILE_WRITE);
        file.print(gVoltageOffset);
        file.close();
        gVoltageCalibrationDone=true;
        delay(5000);
        **************************************************************/
        sendSettings();
        return;
    }

    if (settings.endsWith("bton")) {
        gMicInfo.MicBluetoothOnOrOff = "on";
        btwrite("\n\nbluetooth ON while recording. Use phone to stop record.\n\n");
        writeMicInfo();
        sendSettings();
        return;
    }

    if (settings.endsWith("btoff")) {
        gMicInfo.MicBluetoothOnOrOff = "off";
        btwrite("\n\nbluetooth OFF while recording. Use button to stop record.\n\n");

        writeMicInfo();
        sendSettings();
        return;
    }

    if (settings.endsWith("micinfo")) {

        btwrite("****** micinfo: ******** \nTYPE: " + gMicInfo.MicType + "\nGAIN: " + gMicInfo.MicBitShift + "\nGPSCoords: " + gMicInfo.MicGPSCoords +
                "\nDIRECTION: " + gMicInfo.MicPointingDirectionDegrees + "\nHEIGHT: " + gMicInfo.MicHeight + "\nMOUNT: " + gMicInfo.MicMountType +
                "\nBluetooth when record: " + gMicInfo.MicBluetoothOnOrOff);
        btwrite("\n");
        sendSettings();
        return;
    }

    if (settings.endsWith("help")) {

        // btwrite("\n***commands***\nXXsetgain (11=forest, 14=Mahout)\nXXXXsettype (set mic type)\nXXXXsetname (set eloc bt name)\nupdate
        // (reboot + upgrade firmware)\nbtoff BT off when record\nbton BT on when record\ndelete (don't use)\n\n");
        sendSettings();
        return;
    }

    if (settings.endsWith("settype")) {
        gMicInfo.MicType = settings.substring(settings.lastIndexOf('#') + 1, settings.length() - 7);
        gMicInfo.MicType.trim();
        if (gMicInfo.MicType.length() == 0)
            gMicInfo.MicType = "ns";
        writeMicInfo();
        btwrite("Mic Type is now " + gMicInfo.MicType);
        sendSettings();
        return;
    }

    if (settings.endsWith("setgain")) {
        gMicInfo.MicBitShift = settings.substring(settings.lastIndexOf('#') + 1, settings.length() - 7);
        gMicInfo.MicBitShift.trim();
        btwrite(gMicInfo.MicBitShift);
        if (gMicInfo.MicBitShift == "11" || gMicInfo.MicBitShift == "12" || gMicInfo.MicBitShift == "13" || gMicInfo.MicBitShift == "14" || gMicInfo.MicBitShift == "15" ||
            gMicInfo.MicBitShift == "16") {
        } else {
            btwrite("Error, mic gain out of range. (11 to 16) ");
            gMicInfo.MicBitShift = "11";
        }

        writeMicInfo();
        // int temp=gMicInfo.MicBitShift.toInt();
        btwrite("Mic gain is now " + gMicInfo.MicBitShift);
        sendSettings();
        return;
    }

    if (settings.endsWith("update")) {
        // updateFirmware();
        File temp = SPIFFS.open("/update.txt", "w");
        temp.close();

        btwrite("\nEloc will restart for firmware update. Please re-connect in 1 minute.\n");
        delay(1000);
        ESP.restart();
        return;
    }

    if (settings.endsWith("setname")) {
        String temp;
        temp = settings.substring(settings.lastIndexOf('#') + 1, settings.length() - 7);
        temp.trim();
        // temp=settings.lastIndexOf('#');

        File file = SPIFFS.open("/nodename.txt", FILE_WRITE);
        file.print(temp);

        file.close();
        Serial.println("new name: " + temp);
        btwrite("new name " + temp + "\n\n--- Restarting ELOC ----");
        vTaskDelay(pdMS_TO_TICKS(100));
        sendSettings();
        // readSettings();
        vTaskDelay(pdMS_TO_TICKS(500));
        ESP.restart();
        return;
    }

    //   if (settings.endsWith("sync")) {

    //       // btwrite("syncnow");
    //       // btwrite("syncing with time.google.com");
    //       // vTaskDelay(pdMS_TO_TICKS(5200));
    //       sendSettings();

    //        return;
    // }

    if (settings.endsWith("delete")) {

        // SPIFFS.
        SPIFFS.remove("/settings.txt");
        SPIFFS.remove("/nodename.txt");
        SPIFFS.remove("/micinfo.txt");

        btwrite("spiffs settings removed");
        vTaskDelay(pdMS_TO_TICKS(100));
        sendSettings();

        return;
    }

    File file = SPIFFS.open("/settings.txt", FILE_WRITE);

    if (!file) {
        printf("There was an error opening the file for writing");
        return;
    }

    /*if(file.print(settings)){
        printf("File was written");;
    } else {
        printf("File write failed");
    }*/

    file.print(settings);

    file.close();
}

String getSubstring(String data, char separator, int index) {
    int found = 0;
    int strIndex[] = {0, -1};
    int maxIndex = data.length() - 1;

    for (int i = 0; i <= maxIndex && found <= index; i++) {
        if (data.charAt(i) == separator || i == maxIndex) {
            found++;
            strIndex[0] = strIndex[1] + 1;
            strIndex[1] = (i == maxIndex) ? i + 1 : i;
        }
    }
    return found > index ? data.substring(strIndex[0], strIndex[1]) : "";
}

void readSettings() {

    // SPIFFS.remove("/settings.txt");
    // vTaskDelay(pdMS_TO_TICKS(100));

    if (!(SPIFFS.exists("/settings.txt"))) {
        writeSettings("#settings#" + String(gSampleRate) + "#" + String(gSecondsPerFile) + "#" + gLocation);
        printf("wrote settings to spiffs");
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    File file2 = SPIFFS.open("/settings.txt");

    if (!file2) {

        printf("Failed to open file for reading");
        return;
    }

    // String temp = file2.readStringUntil('\n');

    String temp = file2.readString();
    temp.trim();

    gSampleRate = getSubstring(temp, '#', 2).toInt();
    // temp
    // if (gSampleRate==44100) gSampleRate=48000;
    gSecondsPerFile = getSubstring(temp, '#', 3).toInt();
    gLocation = getSubstring(temp, '#', 4);
    gLocation.trim();

    Serial.println("settings read: " + temp);

    /*printf("File Content:");

    while(file2.available()){

        Serial.write(file2.read());
    }*/

    file2.close();
}



/**************************************************************************************************/


/*************************** Global settings via config file **************************************/
//BUGME: handle this through file system
extern bool gMountedSDCard;

//BUGME: encapsulate these in a struct & implement a getter
int gbitShift;
bool TestI2SClockInput=false;
bool gTimingFix=false;
bool gListenOnly=true;
bool gUseAPLL=true;
int gMaxFrequencyMHZ=80;    // SPI this fails for anyting below 80   //
int gMinFrequencyMHZ=10;
bool gEnableLightSleep=true; //only for AUTOMATIC light leep.

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
                    gMaxFrequencyMHZ = atoi(position + 1);
                    ESP_LOGI(TAG, "Max Frequency MHZ override %d", gMaxFrequencyMHZ);
                }
                position = strstr(line, "MinFrequencyMHZ:");
                if (position != NULL) {
                    position = strstr(line, ":");
                    gMinFrequencyMHZ = atoi(position + 1);
                    ESP_LOGI(TAG, "Min Frequency MHZ override %d", gMinFrequencyMHZ);
                }

                position = strstr(line, "gain:");
                if (position != NULL) {
                    position = strstr(line, ":");
                    gbitShift = atoi(position + 1);
                    ESP_LOGI(TAG, "gain override: %d", gbitShift);
                }

                position = strstr(line, "SecondsPerFile:");
                if (position != NULL) {
                    position = strstr(line, ":");
                    gSecondsPerFile = atoi(position + 1);
                    ESP_LOGI(TAG, "Seconds per File override: %d", gSecondsPerFile);
                }

                position = strstr(line, "UseAPLL:no");
                if (position != NULL) {
                    gUseAPLL = false;
                }
                position = strstr(line, "UseAPLL:yes");
                if (position != NULL) {
                    gUseAPLL = true;
                }
                position = strstr(line, "TryLightSleep?:yes");
                if (position != NULL) {
                    gEnableLightSleep = true;
                }
                position = strstr(line, "TryLightSleep?:no");
                if (position != NULL) {
                    gEnableLightSleep = false;
                }
                position = strstr(line, "ListenOnly?:no");
                if (position != NULL) {
                    gListenOnly = false;
                }
                position = strstr(line, "ListenOnly?:yes");
                if (position != NULL) {
                    gListenOnly = true;
                }
                position = strstr(line, "TimingFix:no");
                if (position != NULL) {
                    gTimingFix = false;
                }
                position = strstr(line, "TimingFix:yes");
                if (position != NULL) {
                    gTimingFix = true;
                }
                position = strstr(line, "TestI2SClockInput:no");
                if (position != NULL) {
                    TestI2SClockInput = false;
                }
                position = strstr(line, "TestI2SClockInput:yes");
                if (position != NULL) {
                    TestI2SClockInput = true;
                }
            }
            ESP_LOGI(TAG, "Use APLL override: %d", gUseAPLL);
            ESP_LOGI(TAG, "Enable light Sleep override: %d", gEnableLightSleep);
            ESP_LOGI(TAG, "Listen Only override: %d", gListenOnly);
            ESP_LOGI(TAG, "Timing fix override: %d", gTimingFix);
            ESP_LOGI(TAG, "Test i2sClockInput: %d", TestI2SClockInput);
            ESP_LOGI(TAG, "\n\n\n");

            fclose(f);
        }
    }

    i2s_mic_Config.sample_rate = gSampleRate;
    if (gSampleRate <= 32000) { // my wav files sound wierd if apll clock raate is > 32kh. So force non-apll clock if >32khz
        i2s_mic_Config.use_apll = gUseAPLL;
        ESP_LOGI(TAG, "Sample Rate is < 32khz USE APLL Clock %d", gUseAPLL);
    } else {
        i2s_mic_Config.use_apll = false;
        ESP_LOGI(TAG, "Sample Rate is > 32khz Forcing NO_APLL ");
    }
}

void readCurrentSession() {
    char line[128] = "";
    char *position;

    FILE *f = fopen("/spiffs/currentsession.txt", "r");
    if (f == NULL) {
        // gSessionFolder="";
        ESP_LOGI(TAG, "Failed to open file for reading");
        // return;
    } else {

        // char *test=line;

        while (!feof(f)) {
            fgets(line, sizeof(line), f);
            // ESP_LOGI(TAG, "%s", line);
            // char *test2;
            // int start,finish;
            position = strstr(line, "Session ID");
            if (position != NULL) {
                position = strstr(line, "_"); // need to ad one memory location
                //
                // the long epoch will be in the first 10 digits. int microseconds the last 3

                gStartupTime = atoll(position + 1);

                char epoch[10] = "";
                epoch[0] = *(position + 1);
                epoch[1] = *(position + 2);
                epoch[2] = *(position + 3);
                epoch[3] = *(position + 4);
                epoch[4] = *(position + 5);
                epoch[5] = *(position + 6);
                epoch[6] = *(position + 7);
                epoch[7] = *(position + 8);
                epoch[8] = *(position + 9);
                epoch[9] = *(position + 10);

                char ms[3] = "";
                ms[0] = *(position + 11);
                ms[1] = *(position + 12);
                ms[2] = *(position + 13);

                // ESP_LOGI(TAG, "epoch %s",  epoch);
                // ESP_LOGI(TAG, "ms %s",  ms);

                setTime(atol(epoch), atoi(ms));
                ESP_LOGI(TAG, "startup time %lld", gStartupTime);
                // ESP_LOGI(TAG, "testing atol %ld",  atol("1676317685250"));
                //  ESP_LOGI(TAG, "testing atoll %lld",  atoll("1676317685250"));
                position = strstr(line, ":  ");
                ESP_LOGI(TAG, "session FOLDER 1 %s", position + 3);
                strncat(gSessionFolder, position + 3, 63);
                gSessionFolder[strcspn(gSessionFolder, "\n")] = '\0';
                ESP_LOGI(TAG, "gSessionFolder %s", gSessionFolder);
            }
            position = strstr(line, "Sample Rate:");
            if (position != NULL) {
                position = strstr(line, ":  "); // need to ad one memory location
                gSampleRate = atoi(position + 3);
                // gSampleRate=4000;
                ESP_LOGI(TAG, "sample rate %u", gSampleRate);
            }

            position = strstr(line, "Mic Gain:");
            if (position != NULL) {
                position = strstr(line, ":  "); // need to ad one memory location
                gbitShift = atol(position + 3);
                ESP_LOGI(TAG, "bit shift %d", gbitShift);
            }

            position = strstr(line, "Seconds Per File:");
            if (position != NULL) {
                position = strstr(line, ":  "); // need to ad one memory location
                gSecondsPerFile = atol(position + 3);
                ESP_LOGI(TAG, "seconds per file %d", gSecondsPerFile);
            }
        }
    }

    fclose(f);
}