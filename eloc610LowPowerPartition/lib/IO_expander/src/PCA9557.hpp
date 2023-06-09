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


#ifndef PCAA9557_CPP_
#define PCAA9557_CPP_

#include <stdint.h>
#include "CPPI2C/cppi2c.h"

class PCA9557_IOEXP {
private:
	CPPI2C::I2c& i2c;
protected:
	const uint8_t IO_I2C_SLAVEADDRESS;
private:
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
	static const uint32_t IO_INPUT_PORT_REG = 0x00;
	static const uint32_t IO_OUTPUT_PORT_REG = 0x01;
	static const uint32_t IO_POLARITY_INV_REG = 0x02;
	static const uint32_t IO_CONFIGURATION_REG = 0x03;
#pragma GCC diagnostic pop
public:
	explicit PCA9557_IOEXP(CPPI2C::I2c& i2cInstance, uint8_t addr_bits) :
		i2c(i2cInstance),
		IO_I2C_SLAVEADDRESS(0x18 + addr_bits) {

	}
	bool checkPresence() {
		return i2c.checkPresence(IO_I2C_SLAVEADDRESS);
	}

	void portConfig_Invert_I(uint32_t portMask);

	void portConfig_I (uint32_t portMask);

	void portConfig_O (uint32_t portMask);

	void setOutputPort (uint32_t portMask);

	void clearOutputPort (uint32_t portMask);

	uint8_t readInput ();
};

#endif // PCAA9557_CPP_