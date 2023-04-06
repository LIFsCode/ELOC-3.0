#include "I2SMEMSSampler.h"
#include "soc/i2s_reg.h"
extern int32_t *graw_samples;
extern int  gbitShift;

I2SMEMSSampler::I2SMEMSSampler(
    i2s_port_t i2s_port,
    i2s_pin_config_t &i2s_pins,
    i2s_config_t i2s_config,
    bool fixSPH0645) : I2SSampler(i2s_port, i2s_config)
{
    m_i2sPins = i2s_pins;
    m_fixSPH0645 = fixSPH0645;
}

void I2SMEMSSampler::configureI2S()
{
    if (m_fixSPH0645)
    {
        // FIXES for SPH0645
        REG_SET_BIT(I2S_TIMING_REG(m_i2sPort), BIT(9));
        REG_SET_BIT(I2S_CONF_REG(m_i2sPort), I2S_RX_MSB_SHIFT);
    }

    i2s_set_pin(m_i2sPort, &m_i2sPins);
}

int I2SMEMSSampler::read(int16_t *samples, int count)
{
    // read from i2s
    //grawsamples hard coded to 1000 samples
    size_t bytes_read = 0;
    int samples_read=0;
    int numberOfIterations=count/1000;  //so the others must always be a divisor of 1000
    int temp;
        for (int j = 0; j < numberOfIterations; j++) {
    
                i2s_read(m_i2sPort, graw_samples, sizeof(int32_t) * 1000, &bytes_read, portMAX_DELAY);
                samples_read += bytes_read / sizeof(int32_t);
                temp=0;
                for (int i = j*1000; i < samples_read; i++)
                {
                
                    //samples[i] = (raw_samples[i] & 0xFFFFFFF0) >> 11;
                    //samples[i] = (raw_samples[i] & 0xFFFFFFF0) >> 14; //16 good
                    samples[i] = graw_samples[temp++]  >> gbitShift; //14 mahout, 11 forest
                }
        }
    
    
    
    
    return samples_read;
}
