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
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "../../../include/project_config.h"

#pragma once

namespace uart_eloc {
class UART_ELOC {
 private:
  uart_port_t uart_port = 0;
  bool run_thread = true;

  /**
   * @brief Task to read from the uart
   * @return esp_err_t
  */
  esp_err_t uart_event_task();

  /**
   * @brief Warper to start the task to read from the uart
   * @param _this
   */
  static void start_thread_wrapper(void *_this);

 public:
  /**
   * @brief Construct a new UART object
   */
  UART_ELOC();

   /**
    * @brief Destroy the uart test object
    */
  virtual ~UART_ELOC();

  /**
   * @brief Setup the UART with the respective port
   * @warning Unless the default pins are used, the UART driver will not work during sleep
   * @ref https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/peripherals/uart.html#_CPPv425uart_set_wakeup_threshold11uart_port_ti
   * @ref https://docs.espressif.com/projects/espressif-esp-faq/en/latest/software-framework/peripherals/uart.html#when-using-uart0-as-a-serial-communication-port-for-esp32-what-should-i-pay-attention-to
   * @ref https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/uart.html
   * @param _port The UART port to use
   * @return esp_err_t
   */
  virtual esp_err_t init(uint32_t _port);

  /**
   * @brief Write data to the uart
   * @param data The data to write
   * @param len The length of the data
   * @return esp_err_t
   */
  virtual esp_err_t write_data(const char *data, size_t len);

  /**
   * @brief Directly read from the uart (i.e. not in a task)
   * @return esp_err_t
   */
  esp_err_t read_input();

  /**
   * @brief Start a task to read from the uart
   * @return esp_err_t
   */
  esp_err_t start_thread();

  /**
   * @brief Stop the task to read from the uart
   * @return esp_err_t
   */
  esp_err_t stop_thread();
};

}  // namespace uart_eloc
