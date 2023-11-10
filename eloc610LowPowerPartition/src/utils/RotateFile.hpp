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
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"



/** @brief The code snippet is defining a class called `RotateFile`. 
 * This class is used for managing and rotating log files.
 */
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
    SemaphoreHandle_t mSemaphore;
    StaticSemaphore_t _mSemaphoreBuffer;

public:
    /// @brief Set the filename of the basic logfile. Only allowed when not opened.
    /// @param filename File name of the log file. Rotational files get appended by numbers [1...maxFiles-1]
    /// @return True if successfuly, false if not
    bool setFilename(const char* filename);
    const std::string getFilename() const { 
        return mFilename; 
    }
    /// @brief Sets the max. number of files to keep. Only allowed when not opened.
    /// @param maxFiles Max. Number of stored files. If 1, only 'filename' is stored. 0: unlimited
    /// @return True if successfuly, false if not
    bool setMaxFiles(uint32_t maxFiles);
    uint32_t getMaxFiles() const {
        return mMaxFiles; 
    }
    /// @brief Sets the max. Filesize. Only allowed when not opened.
    /// @param maxFileSize Max. Filesize. If 'filename' exceeds this size it is copied and a new file is created. 0: unlimited
    /// @return True if successfuly, false if not
    bool setMaxFileSize(uint32_t maxFileSize);
    uint32_t getMaxFileSize() const { 
        return mMaxFileSize; 
    }
    /// @brief Set the WriteCacheCycle. Only allowed when not opened.
    /// @param writeCacheCycle Number of Writes to the file after which a fsync() is triggered to flush data to physical storage
    /// @return True if successfuly, false if not
    bool setWriteCacheCycle(uint32_t writeCacheCycle);
    uint32_t getWriteCacheCycle() const { 
        return mWriteCacheCycle; 
    }

private:
    bool needsRotate() const;
    bool rotate();

    std::string getDirectory() const;
    bool makeDirectory();
    /** private versin of write without any locking */
    bool _write(const char* data);
public:
    /// @brief RotateFile Constructor
    /// @param filename File name of the log file. Rotational files get appended by numbers [1...maxFiles-1]
    /// @param maxFiles Max. Number of stored files. If 1, only 'filename' is stored. 0: unlimited
    /// @param maxFileSize Max. Filesize. If 'filename' exceeds this size it is copied and a new file is created. 0: unlimited
    RotateFile(const char* filename, uint32_t maxFiles = 0, uint32_t maxFileSize = 0);
    ~RotateFile(){};

    bool isOpen() const {
        return _fp != NULL;
    }

    /// @brief Opens the logfile as specified by filename. Needs to be executed before accessing it
    /// @return True if successfuly, false if not
    bool open();

    /// @brief closes the current file
    void close();
    
    /// @brief write a string to RotateFile file
    /// @param data string to write
    /// @return True if successfuly, false if not
    bool write(const char* data);

    /// @brief vprintf like printing to RotateFile file
    /// @param fmt printf style format string
    /// @param args arguments
    /// @return True if successfuly, false if not
    bool vprintf(const char *fmt, va_list args);
};



#endif // UTILS_ROTATEFILE_HPP_