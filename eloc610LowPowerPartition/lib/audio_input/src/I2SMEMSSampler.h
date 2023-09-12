#pragma once

#include "I2SSampler.h"

class I2SMEMSSampler : public I2SSampler
{
private:
    i2s_pin_config_t m_i2sPins;
    bool m_fixSPH0645;
    int  mBitShift;
    bool mListenOnly;

protected:
    esp_err_t configureI2S();

public:
    void setBitShift(int bitShift) {
        mBitShift = bitShift;
    }
    void setListenOnly(bool listenOnly) {
        mListenOnly = listenOnly;
    }
    I2SMEMSSampler(
        i2s_port_t i2s_port,
        i2s_pin_config_t &i2s_pins,
        i2s_config_t i2s_config,
        int  bitShift = 11,
        bool listenOnly = false,
        bool fixSPH0645 = false);
    virtual int read(int16_t *samples, int count);
};
