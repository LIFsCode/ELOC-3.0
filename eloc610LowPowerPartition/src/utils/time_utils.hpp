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

#ifndef ELOC610LOWPOWERPARTITION_SRC_UTILS_TIME_UTILS_HPP_
#define ELOC610LOWPOWERPARTITION_SRC_UTILS_TIME_UTILS_HPP_

#include <time.h>
#include <sys/time.h>
#include <esp_log.h>
#include "WString.h"
#include "version.h"

namespace time_utils {
    String uint64ToString(uint64_t);
    int64_t getSystemTimeMS();
    String getSystemTimeMS_string();
    int64_t getSystemTimeSecs();

}  // namespace time_utils

#endif  // ELOC610LOWPOWERPARTITION_SRC_UTILS_TIME_UTILS_HPP_"
