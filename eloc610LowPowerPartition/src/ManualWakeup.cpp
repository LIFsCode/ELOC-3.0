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

#include "config.h"
#include "lis3dh.h"
#include "ManualWakeup.hpp"


static const char *TAG = "ManualWakeup";

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
    uint32_t gpio_num = (uint32_t) args;
    // send an event with GPIO to the interrupt user task
    //ESP_LOGI(TAG, "int_signal_handler");
    xQueueSendFromISR(gpio_evt_queue, &gpio_num, NULL);
}

// User task that fetches the sensor values.

//BUGME: handle global creation of driver instances in a separate class!
static LIS3DH* gLis3DH;

void wakeup_task (void *pvParameters)
{
    uint32_t gpio_num;
    //ESP_LOGI(TAG, "wakeup_task starting...");
    while (1)
    {
        if (xQueueReceive(gpio_evt_queue, &gpio_num, portMAX_DELAY))
        {
            ESP_LOGI(TAG, "Knock knock knocking on Heaveans Door...");
            lis3dh_int_click_source_t click_src = {};

            // get the source of the interrupt and reset *INTx* signals
            gLis3DH->lis3dh_get_int_click_source (&click_src);


            // in case of click detection interrupt   
            if (click_src.active)
               ESP_LOGI(TAG, "LIS3DH %s\n", 
                      click_src.s_click ? "single click" : "double click");
        }
    }
}


esp_err_t ManualWakeupConfig(CPPI2C::I2c& i2c) {

    ESP_LOGI(TAG, "Creating LIS3DH instance...");
    static LIS3DH lis3dh(i2c, LIS3DH_I2C_ADDRESS_2);
    gLis3DH = &lis3dh;
    esp_err_t err = lis3dh.lis3dh_init_sensor();
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "Creating LIS3DH failed with %s", esp_err_to_name(err));
        return err;
        //TODO: throw error code and abort
    }
    ESP_LOGI(TAG, "Creating LIS3DH done.");


    /** --- INTERRUPT CONFIGURATION PART ---- */
    
    // Interrupt configuration has to be done before the sensor is set
    // into measurement mode to avoid losing interrupts

    // create an event queue to send interrupt events from interrupt
    // handler to the interrupt task
    gpio_evt_queue = xQueueCreate(10, sizeof(uint8_t));

    // configure interupt pins for *INT1* and *INT2* signals and set the interrupt handler
    gpio_set_direction(LIS3DH_INT_PIN, GPIO_MODE_INPUT);
    gpio_pulldown_en(LIS3DH_INT_PIN);
    gpio_pullup_dis(LIS3DH_INT_PIN);
    gpio_set_intr_type(LIS3DH_INT_PIN, GPIO_INTR_POSEDGE);

    xTaskCreate(wakeup_task, "Click Sense Wakeup", 2048, NULL, 1, NULL);

    gpio_install_isr_service(ESP_INTR_FLAG_LEVEL1);
    gpio_isr_handler_add(LIS3DH_INT_PIN, int_signal_handler, (void *)LIS3DH_INT_PIN);

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