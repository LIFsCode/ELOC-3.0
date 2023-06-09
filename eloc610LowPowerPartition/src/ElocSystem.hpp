/*
 * Created on Sun Apr 16 2023
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

#ifndef ELOCSYSTEM_HPP_
#define ELOCSYSTEM_HPP_

#include "CPPI2C/cppi2c.h"
#include "ELOC_IOEXP.hpp"
#include "lis3dh.h"

class ElocSystem
{
private:
    /* data */
    ElocSystem();
    CPPI2C::I2c* mI2CInstance;
    ELOC_IOEXP* mIOExpInstance;
    LIS3DH* mLis3DH;
public:
    inline static ElocSystem& GetInstance() {
        static ElocSystem System;
        return System;
    }
    ~ElocSystem();
    inline CPPI2C::I2c& getI2C() {
        assert(mI2CInstance != NULL);
        return *mI2CInstance;
    }
    inline ELOC_IOEXP& getIoExpander() {
        assert(mIOExpInstance != NULL);
        return *mIOExpInstance;
    }
    inline LIS3DH& getLIS3DH() {
        assert(mLis3DH != NULL);
        return *mLis3DH;
    }
    inline bool hasI2C() const {
        return mI2CInstance != NULL;
    }
    inline bool hasIoExpander() const {
        return mIOExpInstance != NULL;
    }
    inline bool hasLIS3DH() const {
        return mLis3DH != NULL;
    }
};




#endif // ELOCSYSTEM_HPP_