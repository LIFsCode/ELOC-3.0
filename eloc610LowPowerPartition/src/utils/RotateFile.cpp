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

#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_log.h"
#include "ffsutils.h"
#include "ScopeGuard.hpp"
#include "RotateFile.hpp"

/// @brief Timeout for acquiring the Mutex for accessing RotateFile
static const uint32_t LOCK_TIMEOUT_MS = 10;
/// @brief Number of Writes to the file after which a fsync() is triggered to flush data to physical storage
static const uint32_t WRITE_CACHE_CYCLE = 5;

// wrapper for xSemaphoreGive: required for ScopeGuards MakeGuard, which requires a function pointer
// xSemaphoreGive is only a macro
static inline BaseType_t SemaphoreGive(SemaphoreHandle_t xSemaphore) {
    return xSemaphoreGive(xSemaphore);
}

RotateFile::RotateFile(const char* filename, uint32_t maxFiles /*=0*/, uint32_t maxFileSize /*=0*/):
    mFilename(filename), mMaxFiles(maxFiles), mMaxFileSize(maxFileSize), mWriteCacheCycle(WRITE_CACHE_CYCLE), 
    _fp(NULL), mWriteCounter(0), mFatalError(false), mSemaphore(NULL)
{
    this->mFolder = getDirectory();
    // Semaphore cannot be used before a call to xSemaphoreCreateBinary() or
    // xSemaphoreCreateBinaryStatic().
    // The semaphore's data structures will be placed in the xSemaphoreBuffer
    // variable, the address of which is passed into the function.  The
    // function's parameter is not NULL, so the function will not attempt any
    // dynamic memory allocation, and therefore the function will not return
    // return NULL.
    mSemaphore = xSemaphoreCreateMutexStatic ( &_mSemaphoreBuffer );
    // Semaphore is created in the 'empty' state, meaning the semaphore must first be given
    SemaphoreGive(mSemaphore);
}

bool RotateFile::setFilename(std::string filename) { 
    if (!_fp) {    
        this->mFilename = filename; 
        this->mFolder = getDirectory();
        return true;
    }
    return false;
}

bool RotateFile::setMaxFiles(uint32_t maxFiles) { 
    if (!_fp) {    
        this->mMaxFiles = maxFiles; 
        return true;
    }
    return false;
}

bool RotateFile::setMaxFileSize(uint32_t maxFileSize) { 
    if (!_fp) {    
        this->mMaxFileSize = maxFileSize; 
        return true;
    }
    return false;
}
bool RotateFile::setWriteCacheCycle(uint32_t writeCacheCycle) { 
    if (!_fp) {    
        this->mWriteCacheCycle = writeCacheCycle; 
        return true;
    }
    return false;
}

bool RotateFile::needsRotate() const {
    if ((mMaxFileSize != 0) && (ffsutil::getFileSize(mFilename.c_str()) >= mMaxFileSize)) {
        return true;
    }
    return false;
}


bool RotateFile::rotate() { 

    std::string filename;
    std::string filename2;
    if (needsRotate()) {
        filename = mFilename + std::to_string(mMaxFiles-1);
        if (ffsutil::fileExist(filename.c_str())) {
            if (remove(filename.c_str())) {
                printf("%s() ERROR. failed to delete %s, errno=%d (%s) \n", __FUNCTION__, filename.c_str(), errno, strerror(errno));
            }
        }
        for (int i=mMaxFiles-2; i > 0; i--) {
            filename = mFilename + std::to_string(i);
            filename2 = mFilename + std::to_string(i+1);
            if (ffsutil::fileExist(filename.c_str())) {
                if (rename(filename.c_str(), filename2.c_str())) {
                    printf("%s() ERROR. failed to rename %s, errno=%d (%s) \n", __FUNCTION__, filename.c_str(), errno, strerror(errno));
                }
            }
        }
        // sync everything to filesystem before cosing the file
        fsync(fileno(_fp));
        fclose(_fp);
        if (mMaxFiles > 1 ) {
            filename = mFilename + std::to_string(1);
            if (rename(mFilename.c_str(), filename.c_str())) {
                printf("%s() ERROR. failed to rename %s, errno=%d (%s) \n", __FUNCTION__, mFilename.c_str(), errno, strerror(errno));
            }
        }
        else {
            if (remove(mFilename.c_str())) {
                printf("%s() ERROR. failed to delete %s, errno=%d (%s) \n", __FUNCTION__, mFilename.c_str(), errno, strerror(errno));
            }
        }
        _fp = fopen(mFilename.c_str(), "a+");
        if (!_fp) {
            printf("%s() ABORT. file handle _log_remote_fp is NULL\n", __FUNCTION__);
            return false;
        }
        mWriteCounter = 0;
    }
    return true; 
}

bool RotateFile::_write(const char *data) { 
    if (!rotate()) {
        printf("%s() failed to rotate file\n", __FUNCTION__);
    }
    if (_fp == NULL) {
        printf("%s() ABORT. file handle _log_remote_fp is NULL\n", __FUNCTION__);
        return false;
    }
    return (fprintf(_fp, data) >= 0);
}

std::string RotateFile::getDirectory()const { 
    std::string directory;
    const size_t last_slash_idx = mFilename.rfind('/');
    if (std::string::npos != last_slash_idx)
    {
        directory = mFilename.substr(0, last_slash_idx);
    }
    return directory;
}

bool RotateFile::makeDirectory() {
    if (!ffsutil::folderExists(mFolder.c_str())) {
        printf("Folder %s does not exist, creating empty folder\n", mFolder.c_str());
        //TODO: mkdir recursively
        if (mkdir(mFolder.c_str(), 0777) != 0) {
            printf("Failed to create Folder %s\n", mFolder.c_str());
            return false;
        }
        ffsutil::printListDir("/sdcard");
    }
    return true;
}

bool RotateFile::open() {
    if( xSemaphoreTake( mSemaphore, ( TickType_t ) 10 ) == pdTRUE ) {
        MakeGuard(SemaphoreGive, mSemaphore);
        makeDirectory();
        _fp = fopen(mFilename.c_str(), "a+");
        if (!_fp) {
            printf("%s() ABORT. file handle _log_remote_fp is NULL\n", __FUNCTION__);
            return false;
        }
        mWriteCounter = 0;
        // always write a empty line after opening to avoid continuing an old deprecated line
        return _write("\n");
    }
    else {
        printf("%s() ABORT. Semaphore is blocked\n", __FUNCTION__);
        return false;
    }
}

void  RotateFile::close() {
    if (!_fp) {
        printf("%s() ABORT. file handle _log_remote_fp is NULL\n", __FUNCTION__);
        return;
    }
    if( xSemaphoreTake( mSemaphore, portMAX_DELAY ) == pdTRUE ) {
        MakeGuard(SemaphoreGive, mSemaphore);
        // sync everything to filesystem before cosing the file
        fsync(fileno(_fp));
        fclose(_fp);
        _fp = NULL;
    }
}
bool RotateFile::write(const char *data) { 
    if (_fp == NULL) {
        printf("%s() ABORT. file handle _log_remote_fp is NULL\n", __FUNCTION__);
        return false;
    }
    if( xSemaphoreTake( mSemaphore, pdMS_TO_TICKS(LOCK_TIMEOUT_MS) ) == pdTRUE ) {
        MakeGuard(SemaphoreGive, mSemaphore);
        if (!rotate()) {
            printf("%s() ABORT. Failed to rotate file and open new\n", __FUNCTION__);
            return false;
        }
        return (fprintf(_fp, data) >= 0);
    }
    return true;
}
bool RotateFile::vprintf(const char *fmt, va_list args) { 
    int iresult;

    if (_fp == NULL) {
        printf("%s() ABORT. file handle _log_remote_fp is NULL\n", __FUNCTION__);
        return false;
    }
    if( xSemaphoreTake( mSemaphore, pdMS_TO_TICKS(LOCK_TIMEOUT_MS) ) == pdTRUE ) {
        MakeGuard(SemaphoreGive, mSemaphore);
        if (!rotate()) {
            printf("%s() ABORT. Failed to rotate file and open new\n", __FUNCTION__);
            return false;
        }
        // #1 Write to SPIFFS
        iresult = vfprintf(_fp, fmt, args);
        if (iresult < 0) {
            printf("%s() ABORT. failed vfprintf() -> disable future vfprintf(_log_remote_fp) \n", __FUNCTION__);
            return false;
        }

        // #2 Smart commit after x writes
        mWriteCounter++;
        if (mWriteCounter % mWriteCacheCycle == 0) {
            /////printf("%s() fsync'ing log file on SPIFFS (WRITE_CACHE_CYCLE=%u)\n", WRITE_CACHE_CYCLE);
            fsync(fileno(_fp));
        }
    }
    return true; 
}