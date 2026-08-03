/*
 * Created on Fri Aug 11 2023
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

#ifndef UTILS_LOGGING_HPP_
#define UTILS_LOGGING_HPP_
#include "esp_err.h"

namespace Logging {

esp_err_t init(bool logToSdCard, const String& filename, uint32_t maxFiles, uint32_t maxFileSize);

esp_err_t esp_log_to_scard(bool enable);

/// @brief Stop SD logging for a card that has already been removed
/// @note Use instead of esp_log_to_scard(false) on the hot-swap path: that one takes the file
///       lock with portMAX_DELAY and fsync()s, which on a removed card blocks the calling task
///       for many seconds (long enough to drop a Bluetooth connection).
/// @param lockTimeoutMs how long to wait for a writer still inside the log file
/// @return true when the log file was released and the card is safe to unmount; false means
///         a writer is still in there - call again next cycle
bool abandonSdCard(uint32_t lockTimeoutMs = 200);

esp_err_t printLogConfig(String& buf);
esp_err_t updateConfig(const String& buf);


}

#endif // UTILS_LOGGING_HPP_