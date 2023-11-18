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

#ifndef COMMANDS_CMDADVCALLBACK_HPP_
#define COMMANDS_CMDADVCALLBACK_HPP_

#include "CmdBuffer.hpp"
#include "CmdCallback.hpp"
#include "CmdParser.hpp"

template <size_t STORESIZE>
class CmdAdvCallback : public CmdCallback<STORESIZE> {
  protected:
    CmdParserString m_helpList[STORESIZE];
  public:
    CmdAdvCallback() {
      for (size_t i = 0; i < STORESIZE; i++) {
          m_helpList[i] = "";
      }
    };
    virtual ~CmdAdvCallback() {};    
    size_t getHelp(const CmdParserString*& cmdList, const CmdParserString*& helpList) {
      cmdList = this->m_cmdList;
      helpList = this->m_helpList;
      if (this->m_nextElement >= STORESIZE) {
            return STORESIZE;
      }
      return this->m_nextElement;
    }
    using CmdCallback<STORESIZE>::addCmd;
    bool addCmd(CmdParserString cmdStr, CmdCallFunct cbFunct, CmdParserString helpMsg) {
      // Store is full
      if (this->m_nextElement >= STORESIZE) {
        return false;
      }
      // add to store
      m_helpList[this->m_nextElement] = helpMsg;
      return this->addCmd(cmdStr, cbFunct);
    }


    void updateCmdProcessing(CmdParser *      cmdParser,
        CmdBufferObject *cmdBuffer,
        Stream *         serial);
};


#endif // COMMANDS_CMDADVCALLBACK_HPP_