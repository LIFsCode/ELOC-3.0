/*
 * Created on Fri Oct 20 2023
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
#include <esp_err.h>
#include "WString.h"
#include "CmdResponse.hpp"

CmdResponse::CmdResponse() {
    this->clear();
    // reserve 2kByte for payload to avoid future reallocations
    mReturnValue.Payload.reserve(2048);
    mReturnValue.ErrMsg.reserve(512);
};

void CmdResponse::clear()
{
    mReturnValue.ErrCode = ESP_FAIL; // set to generic fail will be cleared on valid result
    mReturnValue.Payload.clear();
    mReturnValue.Cmd.clear();
    mReturnValue.ID = -1;
    mReturnValue.ErrMsg.clear();
};
void CmdResponse::newCmd(const char* cmd, const char* id) {
    this->clear();
    mReturnValue.Cmd = cmd;
    if (id != nullptr) {
        mReturnValue.ID = strtol (id,NULL,0);
    }
    else {
        mReturnValue.ID = -1;
    }
}

void CmdResponse::setError(esp_err_t errCode, const char* errMsg)
{
    mReturnValue.ErrCode = errCode;
    mReturnValue.ErrMsg = errMsg;
    if (mReturnValue.Payload.length() <= 0) {
        // initialize payload with an empty JSON string to make sure syntax is correct
        mReturnValue.Payload = "\"\""; 
    }
};
void CmdResponse::setResultSuccess(const String &payload)
{
    mReturnValue.Payload = payload;
    this->setResult(static_cast<esp_err_t>(ESP_OK));
}

void CmdResponse::setResultSuccess (const char* payload /*= "\"\""*/) 
{
    mReturnValue.Payload = payload;
    this->setResult(static_cast<esp_err_t>(ESP_OK));
}
