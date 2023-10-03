/*
 * Created on Mon Sep 25 2023
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


#ifndef UTILS_ROTATEFILE_HPP_
#define UTILS_ROTATEFILE_HPP_

#include <string>

class RotateFile
{
private:
    /* config parameters */
    std::string mFilename;
    std::string mFolder;
    uint32_t mMaxFiles;
    uint32_t mMaxFileSize;
    uint32_t mWriteCacheCycle;

    /* data */
    FILE* _fp;
    uint32_t mWriteCounter;
    bool mFatalError;

    void setFilename(std::string filename);
    const std::string getFilename() const { 
        return mFilename; 
    }
    void setMaxFiles(uint32_t maxFiles);
    uint32_t getMaxFiles() const {
        return mMaxFiles; 
    }
    void setMaxFileSize(uint32_t maxFileSize);
    uint32_t getMaxFileSize() const { 
        return mMaxFileSize; 
    }
    void setWriteCacheCycle(uint32_t writeCacheCycle);
    uint32_t getWriteCacheCycle() const { 
        return mWriteCacheCycle; 
    }
    bool needsRotate() const;
    bool rotate();

    std::string getDirectory() const;
    bool makeDirectory();
public:
    RotateFile(const char* filename, uint32_t maxFiles = 0, uint32_t maxFileSize = 0);
    ~RotateFile(){};

    bool open();
    void close();
    bool write(const char* data);
    bool vprintf(const char *fmt, va_list args);
};



#endif // UTILS_ROTATEFILE_HPP_