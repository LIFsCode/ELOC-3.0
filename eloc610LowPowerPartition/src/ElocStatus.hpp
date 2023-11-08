/*
 * Created on Sun Nov 05 2023
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


#ifndef ELOCSTATUS_HPP_
#define ELOCSTATUS_HPP_

#include <stdint.h>
#include "WString.h"

//TODO: All these variables are shared across multiple tasks and must be guarded with mutexes

#define ENUM_MACRO(name, v1, v2)\
    enum class name { v1, v2};\
    constexpr const char *name##Strings[] = { #v1, #v2}; \
    constexpr const char *toString(name value) {  return name##Strings[static_cast<int>(value)]; }

ENUM_MACRO (RecState, IDLE, RECORDING);


/* Recording specific status indicators */
extern RecState gRecording;

extern int64_t gTotalUPTimeSinceReboot;  //esp_timer_get_time returns 64-bit time since startup, in microseconds.
extern int64_t gTotalRecordTimeSinceReboot;
extern int64_t gSessionRecordTime;
extern String gSessionIdentifier;


#endif // ELOCSTATUS_HPP_