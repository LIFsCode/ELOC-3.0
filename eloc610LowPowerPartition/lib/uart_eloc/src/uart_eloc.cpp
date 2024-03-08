/*
 * Created on 27 Feb 2024
 *
 * Project: International Elephant Project (Wildlife Conservation International)
 *
 * The MIT License (MIT)
 * Copyright (c) 2024 Owen O'Hehir
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <stdio.h>
#include <driver/uart.h>
#include <esp_err.h>
#include <esp_log.h>
#include <string.h>
#include <esp_sleep.h>
#include "uart_eloc.h"

static const char *TAG = "UART_ELOC";
static QueueHandle_t uart_queue = NULL;


#define PATTERN_CHR_NUM (3)
#define BUFF_SIZE (1024)

namespace uart_eloc {

UART_ELOC::UART_ELOC() {
    ESP_LOGI(TAG, "Func: %s", __func__);
}

esp_err_t UART_ELOC::init(uint32_t _port) {
    ESP_LOGI(TAG, "Func: %s", __func__);

    uart_port = _port;

    /**
     * @brief 115200, 8N1, no flow control
     * @note If getting garbage data, source_clk may not be set correctly.
     */
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 122,  // 122 bytes = default
        .source_clk = UART_SCLK_REF_TICK,
    };
    int intr_alloc_flags = 0;

    #if CONFIG_UART_ISR_IN_IRAM
        intr_alloc_flags = ESP_INTR_FLAG_IRAM;
    #endif

    if (uart_port == UART_NUM_0) {
        ESP_ERROR_CHECK(uart_set_pin(uart_port, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE,
                                                UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    } else if (uart_port == UART_NUM_1) {
        /**
         * @note Pinout (TX: GPIO32, RX: GPIO21, RTS: Not connected, CTS: Not connected)
         * @note GPIO32 = SCKEN_LORA
         * @note GPIO21 = RXEN_LORA
         */
        ESP_ERROR_CHECK(uart_set_pin(uart_port, 32, 21, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    } else {
        ESP_LOGE(TAG, "Invalid UART port");
        return ESP_FAIL;
    }

    // Install UART driver using an event queue here
    ESP_ERROR_CHECK(uart_driver_install(uart_port, BUFF_SIZE, BUFF_SIZE, 20, &uart_queue, 0));
    ESP_ERROR_CHECK(uart_param_config(uart_port, &uart_config));

    // Set uart pattern detect function.
    uart_enable_pattern_det_baud_intr(uart_port, '+', PATTERN_CHR_NUM, 9, 0, 0);
    // Reset the pattern queue length to record at most 20 pattern positions.
    uart_pattern_queue_reset(uart_port, 20);

    #if defined (CONFIG_PM_SLP_DISABLE_GPIO) && defined (ENABLE_UART_WAKE_FROM_SLEEP)
        #error "UART wakeup feature not possible with CONFIG_PM_SLP_DISABLE_GPIO"
    #endif

    #ifdef ENABLE_UART_WAKE_FROM_SLEEP
    // Allow UART to wake the processor from light sleep
    ESP_ERROR_CHECK(uart_set_wakeup_threshold(uart_port, 2));
    ESP_ERROR_CHECK(esp_sleep_enable_uart_wakeup(uart_port));
    #endif

    return ESP_OK;
}

UART_ELOC::~UART_ELOC() {
    ESP_LOGI(TAG, "Func: %s", __func__);
    uart_driver_delete(uart_port);
}

esp_err_t UART_ELOC::write_data(const char *data, size_t len) {
    auto ret = uart_write_bytes(uart_port, data, len);
    ESP_LOGI(TAG, "Wrote %d bytes", ret);
    return (ret);
}

esp_err_t UART_ELOC::read_input() {
    ESP_LOGV(TAG, "Func: %s", __func__);

    uint8_t data[128];
    int length = 0;
    ESP_ERROR_CHECK(uart_get_buffered_data_len(uart_port, reinterpret_cast<size_t*>(&length)));
    length = uart_read_bytes(uart_port, data, length, 100);

    ESP_LOGI(TAG, "Read %d bytes", length);
    ESP_LOGI(TAG, "Data: %s", data);

    return ESP_OK;
}

esp_err_t UART_ELOC::uart_event_task() {
    uart_event_t event;
    size_t buffered_size;
    uint8_t* dtmp = reinterpret_cast<uint8_t*>(malloc(BUFF_SIZE));

    while (run_thread) {
        //  Waiting for UART event.
        if (xQueueReceive(uart_queue, reinterpret_cast<void * >(&event), (TickType_t)portMAX_DELAY)) {
            bzero(dtmp, BUFF_SIZE);
            switch (event.type) {
                // Event of UART receiving data
                /*We'd better handler data event fast, there would be much more data events than
                other types of events. If we take too much time on data event, the queue might
                be full.*/
                case UART_DATA:
                    {
                        // ESP_LOGI(TAG, "[UART DATA]: %d", event.size);
                        auto len = uart_read_bytes(uart_port, dtmp, event.size, portMAX_DELAY);
                        // uart_write_bytes(uart_port, (const char*) dtmp, len);
                        if (len < 0) {
                            ESP_LOGE(TAG, "Error reading from UART %d", len);
                        } else if (len > 0) {
                            ESP_LOGI(TAG, "[DATA EVT]: Length: %d, %s", len, (char *) dtmp);
                        }
                    }
                    break;
                // Event of HW FIFO overflow detected
                case UART_FIFO_OVF:
                    ESP_LOGI(TAG, "hw fifo overflow");
                    // If fifo overflow happened, you should consider adding flow control for your application.
                    // The ISR has already reset the rx FIFO,
                    // As an example, we directly flush the rx buffer here in order to read more data.
                    uart_flush_input(uart_port);
                    xQueueReset(uart_queue);
                    break;
                // Event of UART ring buffer full
                case UART_BUFFER_FULL:
                    ESP_LOGI(TAG, "ring buffer full");
                    // If buffer full happened, you should consider increasing your buffer size
                    // As an example, we directly flush the rx buffer here in order to read more data.
                    uart_flush_input(uart_port);
                    xQueueReset(uart_queue);
                    break;
                // Event of UART RX break detected
                case UART_BREAK:
                    ESP_LOGI(TAG, "uart rx break");
                    break;
                // Event of UART parity check error
                case UART_PARITY_ERR:
                    ESP_LOGI(TAG, "uart parity error");
                    break;
                // Event of UART frame error
                case UART_FRAME_ERR:
                    ESP_LOGI(TAG, "uart frame error");
                    break;
                // UART_PATTERN_DET
                case UART_PATTERN_DET:
                    {
                        uart_get_buffered_data_len(uart_port, &buffered_size);
                        int pos = uart_pattern_pop_pos(uart_port);
                        ESP_LOGI(TAG, "[UART PATTERN DETECTED] pos: %d, buffered size: %d", pos, buffered_size);
                        if (pos == -1) {
                            // There used to be a UART_PATTERN_DET event, but the pattern position queue
                            // is full so that it can not record the position. We should set a larger
                            // queue size. As an example, we directly flush the rx buffer here.
                            uart_flush_input(uart_port);
                        } else {
                            uart_read_bytes(uart_port, dtmp, pos, 100 / portTICK_PERIOD_MS);
                            uint8_t pat[PATTERN_CHR_NUM + 1];
                            memset(pat, 0, sizeof(pat));
                            uart_read_bytes(uart_port, pat, PATTERN_CHR_NUM, 100 / portTICK_PERIOD_MS);
                            ESP_LOGI(TAG, "read data: %s", dtmp);
                            ESP_LOGI(TAG, "read pat : %s", pat);
                        }
                        break;
                    }
                //  Others
                case UART_DATA_BREAK:
                case UART_EVENT_MAX:
                default:
                    ESP_LOGI(TAG, "uart event type: %d", event.type);
                    break;
            }
        }
    }
    free(dtmp);
    dtmp = NULL;
    vTaskDelete(NULL);

    return ESP_OK;
}

void UART_ELOC::start_thread_wrapper(void *_this) {
  reinterpret_cast<UART_ELOC *>(_this)->uart_event_task();
}

esp_err_t UART_ELOC::start_thread() {
  ESP_LOGV(TAG, "Func: %s", __func__);

  run_thread = true;
  auto ret = xTaskCreatePinnedToCore(this->start_thread_wrapper, "uart_test",
                                    1024 * 3, this, TASK_PRIO_UART_TEST, NULL, TASK_UART_TEST_CORE);

  if (ret != pdPASS) {
    ESP_LOGE(TAG, "Failed to create uart_test task");
    return ret;
  }

  return ret;
}

esp_err_t UART_ELOC::stop_thread() {
  ESP_LOGV(TAG, "Func: %s", __func__);
  run_thread = false;
  return ESP_OK;
}

}  // namespace uart_eloc
