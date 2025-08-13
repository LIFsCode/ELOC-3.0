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

#include "ArduinoJson.h"
#include "WString.h"

#include "SPIFFS.h"

#include "BluetoothSerial.h"
#include "ESP32Time.h"

#include "../../../include/project_config.h"
#include "../../ElocHardware/src/config.h"
#include "lis3dh.h"
#include "strutils.h"
#include "CmdAdvCallback.hpp"
#include "CmdResponse.hpp"
#include "ElocCommands.hpp"
#include "BluetoothServer.hpp"
#include "FirmwareUpdate.hpp"
#include "ElocConfig.hpp"
#include "ElocSystem.hpp"
#include "ElocStatus.hpp"

#include "Battery.hpp"


static const char *TAG = "BluetoothServer";

static const char BT_RESP_TERMINATION = 0x04;
static const uint32_t BT_CMD_VERSION = 2;

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

// the last time a BT node was connected
static int lastBtConnectionTimeS = 0;

static esp_err_t disableBluetooth() {
    gBluetoothEnabled = false;
    SerialBT.end();
    return ESP_OK;
}

static esp_err_t enableBluetooth() {
    String nodeName = getDeviceInfo().nodeName;
    ESP_LOGI(TAG, "Enable BT with node %s", nodeName.c_str());
    SerialBT.begin(nodeName, false);
    vTaskDelay(pdMS_TO_TICKS(100));
    if (SerialBT.isReady()) {
        ESP_LOGI(TAG, "SerialBT is ready ------------------------------------------------------ ");
        lastBtConnectionTimeS = esp_timer_get_time()/1000/1000;
    } else {
        ESP_LOGW(TAG, "SerialBT is NOT ready --------------------------------------------------- ");
        ESP_LOGI(TAG, "Disable BT again!");
        disableBluetooth();
        return ESP_ERR_INVALID_STATE;
    }
    gBluetoothEnabled = true;
    return ESP_OK;
}

void btwrite(const String& theString) {
    // FILE *fp;
    if (!gBluetoothEnabled)
        return;

    // SerialBT.
    if (SerialBT.connected()) {
        SerialBT.println(theString);
        SerialBT.print(static_cast<char>(0x04));
    }
}

extern ESP32Time timeObject;

//BUGME: global constant from config.h


void sendSettings() {

    SerialBT.print("{\"device\" : ");
    SerialBT.print("\"ELOC 3.0\"");
    SerialBT.print(", \"cmdVersion\" : ");
    SerialBT.print(BT_CMD_VERSION);
    SerialBT.println("}");
    SerialBT.print(BT_RESP_TERMINATION);

}


void bt_sendResponse(const CmdResponse& cmdResponse) {
    if (!gBluetoothEnabled)
        return;

    // SerialBT.
    if (SerialBT.connected()) {
        //TODO: check if this can be done via ArduinoJSON without too much stack usage overhead
        SerialBT.print("{\"id\" : ");
        SerialBT.print(cmdResponse.getReturnValue().ID);
        SerialBT.print(", \"ecode\" : ");
        SerialBT.print(cmdResponse.getReturnValue().ErrCode);
        SerialBT.print(", \"error\" : \"");
        SerialBT.print(cmdResponse.getReturnValue().ErrMsg);
        SerialBT.print("\", \"cmd\" : \"");
        SerialBT.print(cmdResponse.getReturnValue().Cmd);
        SerialBT.print("\", \"payload\" : ");
        SerialBT.print(cmdResponse.getReturnValue().Payload);
        SerialBT.println("}");
        SerialBT.print(BT_RESP_TERMINATION);
    }
}

static CmdBuffer<2048> cmdBuffer;
static CmdParser     cmdParser;
static CmdAdvCallback<MAX_COMMANDS> cmdCallback;

void cmd_GetHelp(CmdParser* cmdParser) {
    CmdResponse& resp = CmdResponse::getInstance();
    String& help = resp.getPayload(); // write directly to output buffer to avoid reallocation

    const CmdParserString* cmdStr;
    const CmdParserString* helpMsg;
    size_t numCmds = cmdCallback.getHelp(cmdStr, helpMsg);

    StaticJsonDocument<1024> doc;

    JsonArray commands = doc.createNestedArray("commands");

    for (int i = 0; i < numCmds; i++ ) {
        JsonObject cmd = commands.createNestedObject();

        cmd["cmd"] = cmdStr[i];
        cmd["help"] = helpMsg[i];
    }
    serializeJsonPretty(doc, help);
    resp.setResultSuccess(help);
    return;
}

void wait_for_bt_command() {
    static bool sentSettings = false;

    String serialIN;

    if (wav_writer.get_mode() != WAVFileWriter::Mode::disabled &&
        !getConfig().bluetoothEnableDuringRecord) {
        btwrite("recording");
        btwrite(""); btwrite("");
        btwrite("ELOC will disconnect. USE button to stop recording.");

        btwrite(""); btwrite("");
        vTaskDelay(pdMS_TO_TICKS(300));
        // disable bluetooth
        disableBluetooth();
        return;
    }

    if (SerialBT.connected()) {
        lastBtConnectionTimeS = esp_timer_get_time()/1000/1000;
        if (!sentSettings) {
            vTaskDelay(pdMS_TO_TICKS(200));
            sendSettings();
            sentSettings = true;
        }
        // read data and check if command was entered
        if (cmdBuffer.readSerialChar(&SerialBT))
        {
            // parse command line
            if (cmdParser.parseCmd(&cmdBuffer) != CMDPARSER_ERROR)
            {
                const char* cmd = cmdParser.getCommand();
                CmdResponse& cmdResponse = CmdResponse::getInstance();
                const char* id = cmdParser.getValueFromKey("id");
                cmdResponse.newCmd(cmd, id);
                // search command in store and call function
                // ignore return value "false" if command was not found
                if (!cmdCallback.processCmd(&cmdParser))
                {
                    String msg;
                    ESP_LOGE(TAG, "Invalid Command %s, Received %s", cmd, cmdBuffer.getStringFromBuffer());
                    cmdResponse.setError(ESP_ERR_INVALID_ARG, cmdBuffer.getStringFromBuffer());
                    // TODO: Add cmd response with error message and code
                }
                bt_sendResponse(cmdResponse);
                cmdBuffer.clear();
            }
        }
    } else {
        sentSettings = false;
        // if bluetoothOffTimeoutMs <=0 it is ignored per definition, if no LIS3DH is detected for BT wakeup, BT is continously on
        if ((getConfig().bluetoothOffTimeoutSeconds >= 0) && ElocSystem::GetInstance().hasLIS3DH()) {
            int timeDiff = esp_timer_get_time()/1000/1000 - lastBtConnectionTimeS;
            if (timeDiff >= getConfig().bluetoothOffTimeoutSeconds) {
                ESP_LOGI(TAG, "%d seconds without bluetooth connection, exceeding max. of %d sec! Shutting down Bluetooth.", timeDiff, getConfig().bluetoothOffTimeoutSeconds);
                disableBluetooth();
            }
        }
    }
}

// User task that fetches the sensor values.
void wakeup_task (void *pvParameters)
{
    uint32_t gpio_num;
    int loopcounter = 0;
    //ESP_LOGI(TAG, "wakeup_task starting...");
    if (getConfig().bluetoothEnableAtStart || !ElocSystem::GetInstance().hasLIS3DH()) {
        if (enableBluetooth() != ESP_OK) {
            ESP_LOGE(TAG, "Failed to enable bluetooth!");
        }
    }
    else {
        ESP_LOGI(TAG, "Bluetooth is disabled at start. Use Double-tap to wake BT");
    }

    while (1)
    {
        TickType_t gpio_evt_wait = gBluetoothEnabled ? pdMS_TO_TICKS(30) : pdMS_TO_TICKS(100); // do not wait idle if bluetooth is enabled
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

            if (!gBluetoothEnabled && getConfig().bluetoothEnableOnTapping) {
                ESP_LOGI(TAG, " two following are heap size, total and max alloc ");
                ESP_LOGI(TAG, "     %u", ESP.getFreeHeap());
                ESP_LOGI(TAG, "     %u", ESP.getMaxAllocHeap());
                if (enableBluetooth() != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to enable bluetooth!");
                }
            }
            ElocSystem::GetInstance().notifyStatusRefresh();
        }
        bool btConnected = gBluetoothEnabled ? SerialBT.connected() : false;
        ElocSystem::GetInstance().handleSystemStatus(gBluetoothEnabled, btConnected);

        if (gBluetoothEnabled) {
            wait_for_bt_command();
        }

    }
}


esp_err_t BluetoothServerSetup(bool installGpioIsr) {


    /** setup commands */
    cmdBuffer.setEcho(false);
    cmdParser.setOptKeyValue(true);    // use key value notation for command parameters, not positional
    cmdParser.setOptSeperator('#');    // use '#' as separator not ' ' which is unused in JSON files
    cmdParser.setOptIgnoreQuote(true); // required to read JSON syntax correctly
    cmdCallback.addCmd("getHelp", &cmd_GetHelp, "Print all available command and their help as JSON");
    BtCommands::initCommands(cmdCallback);

    /** --- INTERRUPT CONFIGURATION PART ---- */

    // Interrupt configuration has to be done before the sensor is set
    // into measurement mode to avoid losing interrupts

    // create an event queue to send interrupt events from interrupt
    // handler to the interrupt task
    // a single event is enough, it is only used to trigger the start of BT
    gpio_evt_queue = xQueueCreate(1, sizeof(uint8_t));

    xTaskCreate(wakeup_task, "BT Server", 4096, NULL, TASK_PRIO_CMD, NULL);

    if (!ElocSystem::GetInstance().hasLIS3DH()) {
        ESP_LOGW(TAG, "No LIS3DH avaiable! No possibility to enable BT during runtime! System will enable BT continously to guarantee communication...");
        return ESP_OK;
    }

    LIS3DH& lis3dh = ElocSystem::GetInstance().getLIS3DH();

    // configure interupt pins for *INT1* and *INT2* signals and set the interrupt handler
    gpio_set_direction(LIS3DH_INT_PIN, GPIO_MODE_INPUT);
    gpio_pulldown_en(LIS3DH_INT_PIN);
    gpio_pullup_dis(LIS3DH_INT_PIN);
    gpio_set_intr_type(LIS3DH_INT_PIN, GPIO_INTR_POSEDGE);

    if (installGpioIsr) {
        ESP_ERROR_CHECK(gpio_install_isr_service(GPIO_INTR_PRIO));
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
    // for (int i=0; i<5; i++) {
    //     lis3dh_float_data_t  data;
    //     while(!lis3dh.lis3dh_new_data ());
    //     if (lis3dh.lis3dh_get_float_data (&data)) {}
    //         // max. full scale is +-16 g and best resolution is 1 mg, i.e. 5 digits
    //         ESP_LOGI(TAG, "LIS3DH (xyz)[g] ax=%+7.3f ay=%+7.3f az=%+7.3f\n",
    //                 data.ax, data.ay, data.az);
    // }

    return ESP_OK;
}
