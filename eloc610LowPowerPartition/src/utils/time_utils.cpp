/*
 * Created on Sun 14th Jan 2024
 *
 * Project: International Elephant Project (Wildlife Conservation International)
 *
 * The MIT License (MIT)
 * Copyright (c) 2023 Owen O'Hehir
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

#include "time_utils.hpp"

static const char *TAG = "time_utils";

namespace time_utils {

    // String uint64ToString(uint64_t input) {
    //     ESP_LOGV(TAG, "Func: %s", __func__);

    //     String result = "";
    //     const uint8_t base = 10;

    //     do {
    //         char c = input % base;
    //         input /= base;

    //         if (c < 10)
    //             c += '0';
    //         else
    //             c += 'A' - 10;
    //         result = c + result;
    //     } while (input);

    //     return result;
    // }

    // void time() {
    //     struct timeval tv_now;
    //     gettimeofday(&tv_now, NULL);
    //     int64_t time_us = ((int64_t)tv_now.tv_sec * 1000000L) + (int64_t)tv_now.tv_usec;
    //     time_us = time_us / 1000;
    // }

    int64_t getSystemTimeMS() {
        ESP_LOGV(TAG, "Func: %s", __func__);
        // https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/system_time.html#get-current-time-in-milliseconds
        struct timeval tv_now;
        gettimeofday(&tv_now, NULL);
        int64_t time_us = ((int64_t)tv_now.tv_sec * 1000000L) + (int64_t)tv_now.tv_usec;
        time_us = time_us / 1000;
        return (time_us);
    }

    String getSystemTimeMS_string() {
        // return(uint64ToString(getSystemTimeMS()));
        return(String(getSystemTimeMS()));
    }

    int64_t getSystemTimeSecs() {
        ESP_LOGV(TAG, "Func: %s", __func__);
        struct timeval tv_now;
        gettimeofday(&tv_now, NULL);
        return (tv_now.tv_sec);
    }

}  // namespace time_utils
