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

#include "esp_err.h"
#include "PCA9557.hpp"

/* PCA9557PW (IO-Expander)  */
class ELOC_IOEXP: private PCA9557_IOEXP {

public:
	/* K0375 specific */
	static const uint32_t LED_STATUS	= 0x01;
	static const uint32_t LED_BATTERY	= 0x02;
	static const uint32_t CHARGE_EN_N	= 0x04;
	static const uint32_t LiION_DETECT	= 0x08;
	// Bit 6-7 is unconnected (spare)
	static const uint32_t NC_IO4 		= 0x10;
	static const uint32_t NC_IO5		= 0x20;
	static const uint32_t NC_IO6		= 0x40;
	static const uint32_t NC_IO7		= 0x80;
	// IO4 is the micro-SD socket's mechanical card-detect switch (input).
	// Read it with getSdDetectLevel(); the level that means "inserted" is board wiring, see
	// SDCARD_DETECT_PRESENT_LEVEL in project_config.h.
	static const uint32_t SD_DETECT		= NC_IO4;
	// IO5 drives a P-channel high-side MOSFET (AO3401A) gating the ATGM336H GPS module VCC.
	// The gate is pulled to +3V3 by R12, so it is ACTIVE-LOW: gate LOW = GPS ON, gate HIGH = GPS OFF.
	// Use setGpsPower(bool) rather than setOutputBit() directly so the inversion is handled for you.
	static const uint32_t GPS_VCC_EN	= NC_IO5;

private:
	esp_err_t init();
	// shadows the state of the IO expander output port
	// consistency is covered since only setOutputBit() is accessible to alter the output states due to private inheritance of PCA6408_IOEXP
	uint8_t outputReg;
	esp_err_t mErrorCode;
public:
	/// @brief Read the last error code
	/// @return error code of the last executed operation
	inline esp_err_t getErrorCode() const{
		return mErrorCode;
	}

	explicit ELOC_IOEXP(CPPI2C::I2c& i2cInstance);

	esp_err_t setOutputBit(uint32_t bit, bool enable);
	esp_err_t toggleOutputBit(uint32_t bit);
	esp_err_t chargeBattery(bool enable);
	bool hasLiIonBattery();

	/// @brief Enable/disable the ATGM336H GPS module VCC via the IO5 MOSFET gate
	/// @param enable true = power GPS on, false = power off
	esp_err_t setGpsPower(bool enable);

	/// @brief Raw level of the SD card-detect switch on IO4
	/// @note Polarity is board wiring, not a property of the expander — the caller applies
	///       SDCARD_DETECT_PRESENT_LEVEL. Returns the last read level; an I2C failure reads as 0.
	/// @return true = IO4 high, false = IO4 low
	bool getSdDetectLevel();

};

#endif // ELOC_PCA9557_HPP_
