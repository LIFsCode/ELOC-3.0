/*
 * Created on Sun Mar 03 2024
 *
 * Project: International Elephant Project (Wildlife Conservation International)
 *
 * The MIT License (MIT)
 * Copyright (c) 2024 Fabian Lindner
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

#ifndef ELOCPROCESSFACTORY_HPP_
#define ELOCPROCESSFACTORY_HPP_

#include "esp_err.h"

#ifdef EDGE_IMPULSE_ENABLED
    #include "EdgeImpulse.hpp"              // This file includes trumpet_inferencing.h
#endif
#include "I2SMEMSSampler.h"
#include "WAVFileWriter.h"

class ElocProcessFactory
{
private:
    I2SMEMSSampler mInput;
#ifdef EDGE_IMPULSE_ENABLED
    EdgeImpulse mEdgeImpulse;
#endif
    WAVFileWriter mWav_writer;

    /* data */
public:
    ElocProcessFactory(/* args */);
    ~ElocProcessFactory();

    WAVFileWriter& getWavWriter() { // todo make this const
        return mWav_writer;
    }
    I2SMEMSSampler& getInput() {// todo make this const
        return mInput;
    }
    EdgeImpulse& getEdgeImpulse() {// todo make this const
        return mEdgeImpulse;
    }

    esp_err_t begin();

    esp_err_t end();

    void testInput();


};

extern ElocProcessFactory elocProcessing;


#endif // ELOCPROCESSFACTORY_HPP_
