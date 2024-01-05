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

#include "WString.h"

 #define ENUM_MACRO(name, v0, v1, v2)\
    enum class name { v0, v1, v2};\
    constexpr const char *name##Strings[] = {  #v0, #v1, #v2}; \
    constexpr const char *toString(name value) {  return name##Strings[static_cast<int>(value)]; }

/// @brief Definition of channel selection. NOTE: Changes affect the JSON schema of the configuration!
ENUM_MACRO(MicChannel_t, Left, Right, Stereo);
#undef ENUM_MACRO

/// @brief Holds all the Microphone & recording spedific settings
typedef struct {
    String   MicType;
    int      MicBitShift;
    uint32_t MicSampleRate; // TODO: this should finally be moved to Mic Info for consistency
    bool     MicUseAPLL;
    bool     MicUseTimingFix;
    MicChannel_t MicChannel;
}micInfo_t;

const micInfo_t& getMicInfo();

typedef struct {
    bool logToSdCard;
    String filename;
    uint32_t maxFiles;
    uint32_t maxFileSize;
}logConfig_t;

typedef struct {
    bool detectEnable;
    uint32_t thresholdCnt;
    uint32_t detectWindowMS;
}intruderConfig_t;

/// @brief holds all the device specific configuration settings
typedef struct {
    int  secondsPerFile;
    bool listenOnly;
    int  cpuMaxFrequencyMHZ;    // SPI this fails for anyting below 80   //
    int  cpuMinFrequencyMHZ;
    bool cpuEnableLightSleep; //only for AUTOMATIC light leep.
    bool bluetoothEnableAtStart;
    bool bluetoothEnableOnTapping;
    bool bluetoothEnableDuringRecord;
    int bluetoothOffTimeoutSeconds;
    bool testI2SClockInput;
    logConfig_t logConfig;
    intruderConfig_t IntruderConfig;
}elocConfig_T;

const elocConfig_T& getConfig();

/// @brief Holds all Device Meta data, such as Name, location, etc.
typedef struct {
    String fileHeader;
    String locationCode;
    int locationAccuracy;
    String nodeName;
}elocDeviceInfo_T;
const elocDeviceInfo_T& getDeviceInfo();

/// @brief load configuration from filesystem
void readConfig();

/// @brief write current configuration to file system
/// @return True: success
///         False: failure
bool writeConfig();

void clearConfig();

enum class CfgType {RUNTIME, DEFAULT_CFG};

bool printConfig(String& buf, CfgType cfgType = CfgType::RUNTIME);

esp_err_t updateConfig(const char* buf) ;

#endif // ELOCCONFIG_HPP_