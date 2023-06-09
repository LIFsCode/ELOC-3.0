/*
 * Created on Wed Apr 26 2023
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


#ifndef ELOCCONFIG_HPP_
#define ELOCCONFIG_HPP_


//BUGME: encapsulate these in a struct & implement a getter
#include "WString.h"
typedef struct {
    String MicType;
    String MicBitShift;
    String MicGPSCoords;
    String MicPointingDirectionDegrees;
    String MicHeight;
    String MicMountType;
    String MicBluetoothOnOrOff;
}micInfo_t;


void writeMicInfo();
void readMicInfo();
const micInfo_t& getMicInfo();

//BUGME: encapsulate these in a struct & implement a getter
extern uint32_t gSampleRate;
extern int gSecondsPerFile;
extern String gLocation;
extern int gbitShift;
extern bool gTimingFix;
extern bool gListenOnly;
extern bool gUseAPLL;
extern int gMaxFrequencyMHZ;    // SPI this fails for anyting below 80   //
extern int gMinFrequencyMHZ;
extern bool gEnableLightSleep; //only for AUTOMATIC light leep.
extern bool TestI2SClockInput;



void writeSettings(String settings);
void readSettings();
//TODO: why are there 2 different kind of configs?
void readConfig();

#endif // ELOCCONFIG_HPP_