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

#define ARDUINOJSON_ENABLE_STD_STREAM 1
#include "ArduinoJson.h"
#include "WString.h"

#include "CmdBuffer.hpp"
#include "CmdCallback.hpp"
#include "CmdParser.hpp"
#include "CmdAdvCallback.hpp"

#include <esp_err.h>

static const char *TAG = "CmdAdvCallback";

template <size_t STORESIZE>
bool CmdAdvCallback<STORESIZE>::help(String &helpStr)
{
    helpStr.clear();
    helpStr += "Available commands:";
    for (size_t i = 0; this->checkStorePos(i); i++)
    {
        helpStr += "\t";
        helpStr += this->m_cmdList[i];
        helpStr += " : ";
        helpStr += m_helpList[i];
    }
    // TODO: return help as JSON and check for errors
    /* e.g. something like this
      "commands": [
            {
            "cmd": "getHelp",
            "help": "print help"
            },
            {
            "cmd": "getConfig",
            "help": "Config reader"
            }
        ]
    */
    return true;
}
template <size_t STORESIZE>
bool CmdAdvCallback<STORESIZE>::addCmd(CmdParserString cmdStr, CmdCallFunct cbFunct, CmdParserString helpMsg)
{
    // Store is full
    if (this->m_nextElement >= STORESIZE)
    {
        return false;
    }
    // add to store
    m_helpList[this->m_nextElement] = helpMsg;
    return this->addCmd(cmdStr, cbFunct);
}
