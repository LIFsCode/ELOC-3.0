/*
 * Created on Sun Apr 16 2023
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


#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include <FS.h>

#include "SPIFFS.h"

#include "BluetoothSerial.h"
#include "ESP32Time.h"

#include "config.h"
#include "lis3dh.h"
#include "utils/strutils.h"
#include "BluetoothServer.hpp"
#include "ElocConfig.hpp"
#include "ElocSystem.hpp"

#include "Battery.hpp"


static const char *TAG = "BluetoothServer";


static BluetoothSerial SerialBT;

/**
 * In this case, any of the possible interrupts on interrupt signal *INT1* is
 * used to fetch the data.
 *
 * When interrupts are used, the user has to define interrupt handlers that
 * either fetches the data directly or triggers a task which is waiting to
 * fetch the data. In this example, the interrupt handler sends an event to
 * a waiting task to trigger the data gathering.
 */

static QueueHandle_t gpio_evt_queue = NULL;

void IRAM_ATTR int_signal_handler (void *args)
{
    // We have not woken a task at the start of the ISR.
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    uint32_t gpio_num = (uint32_t) args;
    // send an event with GPIO to the interrupt user task
    //ESP_LOGI(TAG, "int_signal_handler");
    xQueueSendFromISR(gpio_evt_queue, &gpio_num, &xHigherPriorityTaskWoken);

    if( xHigherPriorityTaskWoken ) {
        portYIELD_FROM_ISR ();
    }
}


static bool gBluetoothEnabled = false;

static esp_err_t disableBluetooth() {
    SerialBT.end();
    return ESP_OK;
}

static esp_err_t enableBluetooth() {
    String nodeName = readNodeName();
    ESP_LOGI(TAG, "Enable BT with node %s", nodeName.c_str());
    SerialBT.begin(nodeName, false);
    vTaskDelay(pdMS_TO_TICKS(100));
    if (SerialBT.isReady()) {
        ESP_LOGI(TAG, "SerialBT is ready ------------------------------------------------------ ");
    } else {
        ESP_LOGW(TAG, "SerialBT is NOT ready --------------------------------------------------- ");
        ESP_LOGI(TAG, "Disable BT again!");
        disableBluetooth();
        return ESP_ERR_INVALID_STATE;
    }
    gBluetoothEnabled = true;
    return ESP_OK;
}

void btwrite(String theString) {
    // FILE *fp;
    if (!gBluetoothEnabled)
        return;

    // SerialBT.
    if (SerialBT.connected()) {
        SerialBT.println(theString);
    }
}

//BUGME: place this somewhere else
int64_t getSystemTimeMS();
long getTimeFromTimeObjectMS();
String getProperDateTime();
void freeSpace();

//BUGME: global status
extern bool gRecording;
extern int64_t gTotalUPTimeSinceReboot;  //esp_timer_get_time returns 64-bit time since startup, in microseconds.
extern int64_t gTotalRecordTimeSinceReboot;
extern int64_t gSessionRecordTime;
extern String gSessionIdentifier;
extern float gFreeSpaceGB;
extern ESP32Time timeObject;

//BUGME: global constant from config.h
extern String gFirmwareVersion;

void sendElocStatus() { // compiles and sends eloc config
    /*
    will be based on

    //what kind of things we want to send?
    // distinguish between record now,  no-record and previous record
    - mac address
    - gfirmwareVersion
    - eloc_name
    - android appver
    - timeofday (phone time)
    - ID of ranger who did it
    - bat voltage
    - o sdcard size
    - sdcard free space
    - gLocation
    - gps location

    /// mic info
    - gMicType
    -

    - buffer underruns etc of last record

    // info of last record session: ?
    -buffer underruns
    - record time
    - record type , samplerate, gain, etc
    - max/avg file write time.



    ///////// timings since last boot ////
    - total uptime
    - record time no bluetooth
    - record time bluetooth

    /////// timing since last bat change ////
    - total uptime
    - record time no bluetooth
    - record time bluetooth


    //// if recording was started  ///
    - time of day
    - secondsperfile
    - samplerate
    - gMicBitShift
    - gps coords
    - gps accuracy
    - record type (bluetooth on or off)
    - other record parameters, e.g. cpu freq, apll, etc buffer sizes?

    */

    String sendstring = "statusupdate\n";
    sendstring = sendstring + "Time:  " + getProperDateTime() + "\n";
    sendstring = sendstring + "Ranger:  " + "_@b$_" + "\n";
    // sendstring=sendstring+ "\n\n";
    sendstring = sendstring + "!0!" + readNodeName() + "\n"; // dont change

    sendstring = sendstring + "!1!" + gFirmwareVersion + "\n"; // firmware

    float tempvolts = Battery::GetInstance().getVoltage();
    String temptemp = "FULL";
    if (!Battery::GetInstance().isFull())
        temptemp = "";
    if (Battery::GetInstance().isLow())
        temptemp = "!!! LOW !!!";
    if (Battery::GetInstance().isEmpty())
        temptemp = "turn off";

    if (Battery::GetInstance().isCalibrationDone()) {
        sendstring = sendstring + "!2!" + String(tempvolts) + " v # " + temptemp + "\n"; // battery voltage
    } else {
        sendstring = sendstring + "!2!" + String(tempvolts) + " v " + temptemp + "\n";
    }
    sendstring = sendstring + "!3!" + gLocation + "\n"; // file header

    // was uint64tostring
    sendstring = sendstring + "!4!" + String((float)esp_timer_get_time() / 1000 / 1000 / 60 / 60) + " h" + "\n";
    sendstring =
        sendstring + "!5!" + String(((float)gTotalRecordTimeSinceReboot + gSessionRecordTime) / 1000 / 1000 / 60 / 60) + " h" + "\n";
    sendstring = sendstring + "!6!" + String((float)gSessionRecordTime / 1000 / 1000 / 60 / 60) + " h" + "\n";

    sendstring = sendstring + "!7!" + String(gRecording) + "\n";

    sendstring = sendstring + "!8!" + getMicInfo().MicBluetoothOnOrOff + "\n";

    sendstring = sendstring + "!9!" + String(gSampleRate) + "\n";
    sendstring = sendstring + "!10!" + String(gSecondsPerFile) + "\n";

    sendstring = sendstring + "!11!" + String(gFreeSpaceGB) + "\n";
    sendstring = sendstring + "!12!" + getMicInfo().MicType + "\n";

    sendstring = sendstring + "!13!" + getMicInfo().MicBitShift + "\n";
    sendstring = sendstring + "!14!" + gLocationCode + "\n";
    sendstring = sendstring + "!15!" + gLocationAccuracy + " m\n";

    sendstring = sendstring + "!16!" + gSessionIdentifier + "\n";

    Serial.print(sendstring);
    btwrite(sendstring);
}

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
        setMicBluetoothOnOrOff("on");
        btwrite("\n\nbluetooth ON while recording. Use phone to stop record.\n\n");
        writeMicInfo();
        sendSettings();
        return;
    }

    if (settings.endsWith("btoff")) {
        setMicBluetoothOnOrOff("off");
        btwrite("\n\nbluetooth OFF while recording. Use button to stop record.\n\n");

        writeMicInfo();
        sendSettings();
        return;
    }

    if (settings.endsWith("micinfo")) {

        const micInfo_t& micInfo = getMicInfo();
        btwrite("****** micinfo: ******** \nTYPE: " + micInfo.MicType + "\nGAIN: " + micInfo.MicBitShift + "\nGPSCoords: " + micInfo.MicGPSCoords +
                "\nDIRECTION: " + micInfo.MicPointingDirectionDegrees + "\nHEIGHT: " + micInfo.MicHeight + "\nMOUNT: " + micInfo.MicMountType +
                "\nBluetooth when record: " + micInfo.MicBluetoothOnOrOff);
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
        String MicType = settings.substring(settings.lastIndexOf('#') + 1, settings.length() - 7);
        MicType.trim();
        if (MicType.length() == 0)
            MicType = "ns";
        setMicType(MicType);
        writeMicInfo();
        btwrite("Mic Type is now " + getMicInfo().MicType);
        sendSettings();
        return;
    }

    if (settings.endsWith("setgain")) {
        String MicBitShift = settings.substring(settings.lastIndexOf('#') + 1, settings.length() - 7);
        MicBitShift.trim();
        btwrite(MicBitShift);
        if (MicBitShift == "11" || MicBitShift == "12" || MicBitShift == "13" || MicBitShift == "14" || MicBitShift == "15" ||
            MicBitShift == "16") {
        } else {
            btwrite("Error, mic gain out of range. (11 to 16) ");
            MicBitShift = "11";
        }
        setMicBitShift(MicBitShift);

        writeMicInfo();
        // int temp=gMicInfo.MicBitShift.toInt();
        btwrite("Mic gain is now " + getMicInfo().MicBitShift);
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
        ESP_LOGI(TAG, "new name: %s", temp.c_str());
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

    ESP_LOGI(TAG, "settings read: %s", temp.c_str());

    /*printf("File Content:");

    while(file2.available()){

        Serial.write(file2.read());
    }*/

    file2.close();
}

void wait_for_bt_command() {

    float currentvolts;
    currentvolts = Battery::GetInstance().getVoltage();

    // Battery::GetInstance().getVoltage();
    static bool sentElocStatus = false;
    static bool sentSettings = false;
    static bool sentRecord = false;

    int64_t timein = getSystemTimeMS();

    String serialIN;

    if (gRecording && getMicInfo().MicBluetoothOnOrOff.equalsIgnoreCase("off")) {
        btwrite("recording");
        btwrite("");btwrite("");
        btwrite("ELOC will disconnect. USE button to stop recording.");

        btwrite("");btwrite("");
        vTaskDelay(pdMS_TO_TICKS(300));
        //disable bluetooth
        disableBluetooth();
        return;
    }

    if (SerialBT.connected()) {
        if (!sentRecord && gRecording) {
            btwrite("recording");
            vTaskDelay(pdMS_TO_TICKS(200));
            sendElocStatus();
            sentRecord=true;
        }
        if (!sentSettings) {

            // vTaskDelay(pdMS_TO_TICKS(200));
            vTaskDelay(pdMS_TO_TICKS(200));
            sendSettings();
            vTaskDelay(pdMS_TO_TICKS(200));
            freeSpace();
            vTaskDelay(pdMS_TO_TICKS(200));
            btwrite("getClk\n");
            // vTaskDelay(pdMS_TO_TICKS(50));
            // vTaskDelay(pdMS_TO_TICKS(800));
            // sendElocStatus();
            // if (gFreeSpaceGB!=0.0) btwrite("SD card free: "+String(gFreeSpaceGB)+" GB");
            // vTaskDelay(pdMS_TO_TICKS(100));

            // btwrite(SD);
            sentSettings = true;
            // vTaskDelay(pdMS_TO_TICKS(200));

            // btwrite("#"+String(gSampleRate)+"#"+String(gSecondsPerFile)+"#"+gLocation);
        }
        // gotCommand=false;
        if (SerialBT.available()) {
            // handle case for sending initial default setup to app
            serialIN = SerialBT.readString();
            // ESP_LOGI(TAG, serialIN);
            // if (serialIN.startsWith("settingsRequest")) {
            //    btwrite("#"+String(gSampleRate)+"#"+String(gSecondsPerFile)+"#"+gLocation);
            // }

            if (serialIN.startsWith("record")) {
                btwrite("\n\nYou are using an old version of the Android app. Please upgrade\n\n");
            }

            if (serialIN.startsWith("_setClk_")) {
                ESP_LOGI(TAG, "setClk starting");
                // string will look like _setClk_G__120____32456732728  //if google, 12 mins since last phone sync
                //                    or _setClk_P__0____43267832648  //if phone, 0 min since last phone sync

                String everything = serialIN.substring(serialIN.indexOf("___") + 3, serialIN.length());
                everything.trim();
                String seconds = everything.substring(0, 10); // was 18
                String milliseconds = everything.substring(10, everything.length());
                String tmp = "timestamp in from android GMT " + everything + "  sec: " + seconds + "   millisec: " + milliseconds;
                ESP_LOGI(TAG, "%s", tmp.c_str());
                String minutesSinceSync = serialIN.substring(11, serialIN.indexOf("___"));
                gSyncPhoneOrGoogle = serialIN.substring(8, 9);
                // ESP_LOGI(TAG, minutesSinceSync);
                // ESP_LOGI(TAG, "GorP: "+GorP);
                // delay(8000);
                // ESP_LOGI(TAG, test);
                // ESP_LOGI(TAG, seconds);
                // ESP_LOGI(TAG, milliseconds);
                milliseconds.trim();
                if (milliseconds.length() < 2)
                    milliseconds = "0";
                timeObject.setTime(atol(seconds.c_str()) + (TIMEZONE_OFFSET * 60L * 60L), (atol(milliseconds.c_str())) * 1000);
                // timeObject.setTime(atol(seconds.c_str()),  (atol(milliseconds.c_str()))*1000    );
                //  timestamps coming in from android are always GMT (minus 7 hrs)
                //  if I not add timezone then timeobject is off
                //  so timeobject does not seem to be adding timezone to system time.
                //  timestamps are in gmt+0, so timestamp convrters

                struct timeval tv_now;
                gettimeofday(&tv_now, NULL);
                int64_t time_us = ((int64_t)tv_now.tv_sec * 1000000L) + (int64_t)tv_now.tv_usec;
                time_us = time_us / 1000;

                // ESP_LOGI(TAG, "atol(minutesSinceSync.c_str()) *60L*1000L "+String(atol(minutesSinceSync.c_str()) *60L*1000L));
                gLastSystemTimeUpdate = getTimeFromTimeObjectMS() - (atol(minutesSinceSync.c_str()) * 60L * 1000L);
                timein = getSystemTimeMS();
                // ESP_LOGI(TAG, "timestamp in from android GMT "+everything    +"  sec: "+seconds + "   millisec: "+milliseconds);
                // ESP_LOGI("d", "new timestamp from new sys time (local time) %lld", time_us  ); //this is 7 hours too slow!
                // ESP_LOGI("d","new timestamp from timeobJect (local time) %lld",gLastSystemTimeUpdate);

                // btwrite("time: "+timeObject.getDateTime()+"\n");
                if (!sentElocStatus) {
                    sentElocStatus = true;
                    sendElocStatus();
                }
                ESP_LOGI(TAG, "setClk ending");
            }

            if (serialIN.startsWith("setGPS")) {
                // read the location on startup?
                // only report recorded location status?
                // need to differentiate between manual set and record set.
                gLocationCode = serialIN.substring(serialIN.indexOf("^") + 1, serialIN.indexOf("#"));
                gLocationCode.trim();
                gLocationAccuracy = serialIN.substring(serialIN.indexOf("#") + 1, serialIN.length());
                gLocationAccuracy.trim();
                ESP_LOGI(TAG, "loc: %s   acc %s", gLocationCode.c_str(), gLocationAccuracy.c_str());

                File file = SPIFFS.open("/gps.txt", FILE_WRITE);
                file.println(gLocationCode);
                file.println(gLocationAccuracy);
                ESP_LOGI(TAG, "Starting Recording...");
                rec_req_t rec_req = REC_REQ_START;
                xQueueSend(rec_req_evt_queue, &rec_req, NULL);

                // btwrite("GPS Location set");
            }
            if (serialIN.startsWith("stoprecord")) {

                ESP_LOGI(TAG, "Stopping Recording...");
                rec_req_t rec_req = REC_REQ_STOP;
                xQueueSend(rec_req_evt_queue, &rec_req, NULL);
            }

            if (serialIN.startsWith("_record_")) {

                ESP_LOGI(TAG, "Starting Recording...");
                rec_req_t rec_req = REC_REQ_START;
                xQueueSend(rec_req_evt_queue, &rec_req, NULL);
            } else {
                // const char *converted=serialIN.c_str();
                /* if (serialIN.startsWith( "8k")){gSampleRate=8000;btwrite("sample rate changed to 8k");gotCommand=true;}
                if (serialIN.startsWith( "16k")){gSampleRate=16000;btwrite("sample rate changed to 16k");gotCommand=true;}
                if (serialIN.startsWith( "22k")){gSampleRate=22000;btwrite("sample rate changed to 22k");gotCommand=true;}
                if (serialIN.startsWith( "32k")){gSampleRate=32000;btwrite("sample rate changed to 32k");gotCommand=true;}
                if (serialIN.startsWith( "10s")){gSecondsPerFile=10;btwrite("10 secs per file");gotCommand=true;}
                if (serialIN.startsWith( "1m")){gSecondsPerFile=60;btwrite("1 minute per file");gotCommand=true;}
                if (serialIN.startsWith( "5m")){gSecondsPerFile=300;btwrite("5 minutes per file");gotCommand=true;}
                if (serialIN.startsWith( "1h")){gSecondsPerFile=3600;btwrite("1 hour per file");gotCommand=true;}
                if (serialIN.startsWith( "settingsRequest")){gotCommand=true;}
                if (!gotCommand) btwrite("command not found. options are 8k 16k 22k 32k  and 10s 1m 5m 1h");
                */

                if (serialIN.startsWith("#settings")) {
                    writeSettings(serialIN);
                    vTaskDelay(pdMS_TO_TICKS(200));
                    readSettings();
                    vTaskDelay(pdMS_TO_TICKS(200));
                    btwrite("settings updated");
                    vTaskDelay(pdMS_TO_TICKS(500));
                    sendElocStatus();
                }
            }
        }
    } else {
        sentSettings = false;
        sentElocStatus = false;
        sentRecord = false;
    }
}

// User task that fetches the sensor values.
void wakeup_task (void *pvParameters)
{
    uint32_t gpio_num;
    int loopcounter = 0;
    //ESP_LOGI(TAG, "wakeup_task starting...");
    if (gEnableBluetoothAtStart) {
        if (enableBluetooth() != ESP_OK) {
            ESP_LOGE(TAG, "Failed to enable bluetooth!");
        }
    }
    else {
        ESP_LOGI(TAG, "Bluetooth is disabled at start. Use Double-tap to wake BT");
    }

    while (1)
    {
        TickType_t gpio_evt_wait = gBluetoothEnabled ? 0 : portMAX_DELAY; // do not wait idle if bluetooth is enabled
        if (xQueueReceive(gpio_evt_queue, &gpio_num, gpio_evt_wait))
        {
            ESP_LOGI(TAG, "Knock knock knocking on Heaveans Door...");
            lis3dh_int_click_source_t click_src = {};

            // get the source of the interrupt and reset *INTx* signals
            ElocSystem::GetInstance().getLIS3DH().lis3dh_get_int_click_source (&click_src);


            // in case of click detection interrupt   
            if (click_src.active)
               ESP_LOGI(TAG, "LIS3DH %s\n", 
                      click_src.s_click ? "single click" : "double click");

            float voltage = Battery::GetInstance().getVoltage();
            ESP_LOGI(TAG, "Bat Voltage %.3f\n", voltage);

            //TODO: check for config to enable bluetooth
            if (!gBluetoothEnabled) {
                ESP_LOGI(TAG, " two following are heap size, total and max alloc ");
                ESP_LOGI(TAG, "     %u", ESP.getFreeHeap());
                ESP_LOGI(TAG, "     %u", ESP.getMaxAllocHeap());
                if (enableBluetooth() != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to enable bluetooth!");
                }
            }
        }

        if (gBluetoothEnabled) {
            wait_for_bt_command();
            

            loopcounter++;
            if (loopcounter == 30)
                loopcounter = 0;
            vTaskDelay(pdMS_TO_TICKS(30)); // so if we get record, max 10ms off



            /**************** NOTE: Status LED blinking must be handled in a separate task
            if (loopcounter==0) {
            currentvolts= Battery::GetInstance().getVoltage();
            //currentvolts=0.1;
            if ((getSystemTimeMS()-timein) > (60000*gMinutesWaitUntilDeepSleep)) doDeepSleep(); // one hour to deep sleep
            if (currentvolts <= gvOff) doDeepSleep();
            digitalWrite(STATUS_LED,HIGH);


            digitalWrite(BATTERY_LED,LOW);
            if (currentvolts <= gvLow) digitalWrite(BATTERY_LED,HIGH);
            if (currentvolts >= gvFull) digitalWrite(BATTERY_LED,HIGH);

            } else {
            if (!SerialBT.connected()) digitalWrite(STATUS_LED,LOW);
            if (currentvolts <= gvLow) digitalWrite(BATTERY_LED,LOW);
            }
            ****************************************************************************/
        }

    }
}


esp_err_t BluetoothServerSetup(bool installGpioIsr) {

    LIS3DH& lis3dh = ElocSystem::GetInstance().getLIS3DH();

    /** --- INTERRUPT CONFIGURATION PART ---- */
    
    // Interrupt configuration has to be done before the sensor is set
    // into measurement mode to avoid losing interrupts

    // create an event queue to send interrupt events from interrupt
    // handler to the interrupt task
    // a single event is enough, it is only used to trigger the start of BT
    gpio_evt_queue = xQueueCreate(1, sizeof(uint8_t));

    // configure interupt pins for *INT1* and *INT2* signals and set the interrupt handler
    gpio_set_direction(LIS3DH_INT_PIN, GPIO_MODE_INPUT);
    gpio_pulldown_en(LIS3DH_INT_PIN);
    gpio_pullup_dis(LIS3DH_INT_PIN);
    gpio_set_intr_type(LIS3DH_INT_PIN, GPIO_INTR_POSEDGE);

    xTaskCreate(wakeup_task, "Bluetooth Server", 4096, NULL, 1, NULL);

    if (installGpioIsr) {
        ESP_ERROR_CHECK(GPIO_INTR_PRIO);
    }
    ESP_ERROR_CHECK(gpio_isr_handler_add(LIS3DH_INT_PIN, int_signal_handler, (void *)LIS3DH_INT_PIN));

    lis3dh.lis3dh_set_int_click_config (&lis3dh_click_config);
    lis3dh.lis3dh_enable_int (lis3dh_int_click, lis3dh_int1_signal, true);


    // configure HPF and reset the reference by dummy read
    lis3dh.lis3dh_config_hpf (lis3dh_hpf_normal, 0, true, true, true, true);
    lis3dh.lis3dh_get_hpf_ref ();

    // enable ADC inputs and temperature sensor for ADC input 3
    lis3dh.lis3dh_enable_adc (true, true);

    // LAST STEP: Finally set scale and mode to start measurements
    lis3dh.lis3dh_set_config(lis3dh_config);
    
    //lis3dh.lis3dh_set_scale(lis3dh_scale_2_g);
    //lis3dh.lis3dh_set_mode (lis3dh_odr_400, lis3dh_high_res, true, true, true);

    // take 5 measurements for debugging purpose
    for (int i=0; i<5; i++) {
        lis3dh_float_data_t  data;
        while(!lis3dh.lis3dh_new_data ());
        if (lis3dh.lis3dh_get_float_data (&data)) {}
            // max. full scale is +-16 g and best resolution is 1 mg, i.e. 5 digits
            ESP_LOGI(TAG, "LIS3DH (xyz)[g] ax=%+7.3f ay=%+7.3f az=%+7.3f\n",
                    data.ax, data.ay, data.az);
    }

    return ESP_OK;
}