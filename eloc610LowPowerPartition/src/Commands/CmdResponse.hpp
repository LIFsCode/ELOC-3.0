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
#ifndef COMMANDS_CMDRESPONSE_HPP_
#define COMMANDS_CMDRESPONSE_HPP_

#include <stdint.h>
#include <esp_err.h>
#include "WString.h"

class CmdResponse
{
private:
    /* data */
    typedef struct {
        uint16_t ErrCode;
        String Payload;
        String Cmd;
    }ReturnValue_t;
    ReturnValue_t mReturnValue;
    CmdResponse();;
    virtual ~CmdResponse() {};
public:
    inline static CmdResponse& getInstance() {
        static CmdResponse instance;
        return instance;
    };
    void clear();
    void setCmd(const char* cmd);
    void setResult(esp_err_t errCode) {
        return setError(errCode, esp_err_to_name(errCode));
    }
    void setError(esp_err_t errCode, const char* errMsg);
    inline const ReturnValue_t& getReturnValue() const {
        return mReturnValue;
    }
    void setResultSuccess (const String& payload);
    void setResultSuccess (const char* payload = "");
    String& getPayload() {
        return mReturnValue.Payload;
    }
};


#endif // COMMANDS_CMDRESPONSE_HPP_