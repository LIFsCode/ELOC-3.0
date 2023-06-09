/*
 * Created on Thu Apr 06 2023
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


#include <stdint.h>

#include <stdio.h>
#include "esp_log.h"
#include "driver/i2c.h"
#include "PCA9557.hpp"

void PCA9557_IOEXP::portConfig_Invert_I (uint32_t portMask) {
	uint8_t reg;
	reg = i2c.ReadRegister (IO_I2C_SLAVEADDRESS, IO_POLARITY_INV_REG);
	uint8_t sendBuf = reg | portMask;
	i2c.WriteRegister (IO_I2C_SLAVEADDRESS, IO_POLARITY_INV_REG, sendBuf);
}

void PCA9557_IOEXP::portConfig_I (uint32_t portMask) {
	uint8_t reg;
	reg = i2c.ReadRegister (IO_I2C_SLAVEADDRESS, IO_CONFIGURATION_REG);
	uint8_t sendBuf = reg | portMask;
	i2c.WriteRegister (IO_I2C_SLAVEADDRESS, IO_CONFIGURATION_REG, sendBuf);
}

void PCA9557_IOEXP::portConfig_O (uint32_t portMask) {
	uint8_t reg;
	reg = i2c.ReadRegister (IO_I2C_SLAVEADDRESS, IO_CONFIGURATION_REG);
	uint8_t sendBuf = reg & ~portMask;
	i2c.WriteRegister (IO_I2C_SLAVEADDRESS, IO_CONFIGURATION_REG, sendBuf);
}

void PCA9557_IOEXP::setOutputPort (uint32_t portMask) {
	uint8_t reg;
	reg = i2c.ReadRegister (IO_I2C_SLAVEADDRESS, IO_OUTPUT_PORT_REG);
	uint8_t sendBuf = reg | portMask;
	i2c.WriteRegister (IO_I2C_SLAVEADDRESS, IO_OUTPUT_PORT_REG, sendBuf);
}

void PCA9557_IOEXP::clearOutputPort (uint32_t portMask) {
	uint8_t reg;
	reg = i2c.ReadRegister (IO_I2C_SLAVEADDRESS, IO_OUTPUT_PORT_REG);
	uint8_t sendBuf = reg & ~portMask;
	i2c.WriteRegister (IO_I2C_SLAVEADDRESS, IO_OUTPUT_PORT_REG, sendBuf);
}

uint8_t PCA9557_IOEXP::readInput () {
	uint8_t reg;
	reg = i2c.ReadRegister (IO_I2C_SLAVEADDRESS, IO_INPUT_PORT_REG);
	return reg;
}
