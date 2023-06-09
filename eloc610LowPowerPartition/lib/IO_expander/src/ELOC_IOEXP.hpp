/*
 * Created on Fri Apr 07 2023
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


#ifndef ELOC_IOEXP_HPP_
#define ELOC_IOEXP_HPP_

#include "PCA9557.hpp"

/* PCA9557PW (IO-Expander)  */
class ELOC_IOEXP: private PCA9557_IOEXP {

public:
	/* K0375 specific */
	static const uint32_t LED_STATUS	= 0x01;
	static const uint32_t LED_BATTERY	= 0x02;
	static const uint32_t CHARGE_EN		= 0x04;
	static const uint32_t LiION_DETECT	= 0x08;
	// Bit 4-7 is unconnected (spare)
	static const uint32_t NC_IO4 		= 0x10;
	static const uint32_t NC_IO5		= 0x20;
	static const uint32_t NC_IO6		= 0x40;
	static const uint32_t NC_IO7		= 0x80;

private:
	void init();
	// shadows the state of the IO expander output port
	// consistency is covered since only setOutputBit() is accessible to alter the output states due to private inheritance of PCA6408_IOEXP
	uint8_t outputReg;
public:

	explicit ELOC_IOEXP(CPPI2C::I2c& i2cInstance);

	void setOutputBit(uint32_t bit, bool enable);
	void toggleOutputBit(uint32_t bit);
	void chargeBattery(bool enable);
	bool hasLiIonBattery();

};

#endif // ELOC_PCA9557_HPP_
