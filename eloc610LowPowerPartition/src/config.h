#pragma once

#ifndef _CONFIG_H_
#define _CONFIG_H_

#include <freertos/FreeRTOS.h>
#include <driver/i2s.h>
#include <driver/adc.h>
#include "lis3dh_types.h"
#include "project_config.h"


#ifdef USE_SDIO_VERSION

        extern lis3dh_config_t lis3dh_config;
        extern lis3dh_int_click_config_t lis3dh_click_config;

#endif

// i2s config for reading from of I2S
extern i2s_config_t i2s_mic_Config;
// i2s microphone pins
extern i2s_pin_config_t i2s_mic_pins;

static const char* const gFirmwareVersion = VERSION;

#endif // _CONFIG_H_