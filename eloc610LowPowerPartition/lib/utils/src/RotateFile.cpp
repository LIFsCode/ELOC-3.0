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
#include <utime.h>

#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "ffsutils.h"
#include "ScopeGuard.hpp"
#include "RotateFile.hpp"
#include "ESP32Time.h"

/// @brief Timeout for acquiring the Mutex for accessing RotateFile
static const uint32_t LOCK_TIMEOUT_MS = 10;
/// @brief Number of Writes to the file after which a fsync() is triggered to flush data to physical storage
static const uint32_t WRITE_CACHE_CYCLE = 5;
/// @brief Number of set-aside damaged files after which no further ones are created
static const int MAX_DAMAGED_FILES = 9;
/// @brief Wait between the clock being set and correcting the file dates: GPS delivers UTC first and the
///        time zone derived from its longitude a moment later, and the dates are stored in local time
static const int64_t CREATION_STAMP_DELAY_US = 5 * 1000 * 1000;

// In RTC memory because the first wake of a duty cycle can create the file long before GPS or the app
// delivers a time.
static RTC_DATA_ATTR bool rtcCreationStampPending = false;

// wrapper for xSemaphoreGive: required for ScopeGuards ON_SCOPE_EXIT, which requires a function pointer
// xSemaphoreGive is only a macro
static inline BaseType_t SemaphoreGive(SemaphoreHandle_t xSemaphore) {
    return xSemaphoreGive(xSemaphore);
}

// fsync() alone only commits what stdio has already passed on and leaves up to a buffer's worth behind
static bool commit(FILE* fp) {
    if (fflush(fp) != 0) {
        return false;
    }
    return fsync(fileno(fp)) == 0;
}

RotateFile::RotateFile(const char* filename, uint32_t maxFiles /*=0*/, uint32_t maxFileSize /*=0*/):
    mFilename(filename), mMaxFiles(maxFiles), mMaxFileSize(maxFileSize), mWriteCacheCycle(WRITE_CACHE_CYCLE), 
    _fp(NULL), mWriteCounter(0), mFatalError(false), mSemaphore(NULL), mDamageHandled(false),
    mCreationStampDueUs(0)
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

bool RotateFile::setFilename(const char* filename) { 
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
        if (!openFile()) {
            printf("%s() ABORT. file handle _log_remote_fp is NULL\n", __FUNCTION__);
            return false;
        }
    }
    return true; 
}

bool RotateFile::openFile() {
    const bool created = !ffsutil::fileExist(mFilename.c_str());
    _fp = fopen(mFilename.c_str(), "a+");
    if (!_fp) {
        return false;
    }
    mWriteCounter = 0;
    if (created) {
        // Created on an unset clock this file now carries the firmware build date, see stampCreationTime()
        rtcCreationStampPending = !ESP32Time::isClockSet();
        mCreationStampDueUs = 0;
    }
    return true;
}

bool RotateFile::replaceDamagedFile(int err) {
    // Once per open(): if a new file fails straight away as well, the file is not what is wrong
    if ((err != EIO) || mDamageHandled) {
        return false;
    }
    mDamageHandled = true;

    if (_fp) {
        fclose(_fp);
        _fp = NULL;
    }
    // Card gone, or SD access failing altogether: the file itself may be fine, so leave it alone
    if (!ffsutil::folderExists(mFolder.c_str())) {
        return false;
    }
    std::string aside;
    for (int i = 1; i <= MAX_DAMAGED_FILES && aside.empty(); i++) {
        const std::string candidate = mFilename + ".damaged" + std::to_string(i);
        if (!ffsutil::fileExist(candidate.c_str())) {
            aside = candidate;
        }
    }
    if (aside.empty() || rename(mFilename.c_str(), aside.c_str()) != 0) {
        printf("%s() ERROR. failed to move %s out of the way, errno=%d (%s) \n", __FUNCTION__, mFilename.c_str(), errno, strerror(errno));
        return false;
    }
    if (!openFile()) {
        printf("%s() ABORT. failed to create a new %s, errno=%d (%s) \n", __FUNCTION__, mFilename.c_str(), errno, strerror(errno));
        return false;
    }
    static const char* NOTE = "\n%s could not be written: moved it to %s and started a new log. The SD card needs a disk check.\n";
    printf(NOTE, mFilename.c_str(), aside.c_str());
    fprintf(_fp, NOTE, mFilename.c_str(), aside.c_str());
    return true;
}

void RotateFile::stampCreationTime() {
    const int64_t nowUs = esp_timer_get_time();
    if (mCreationStampDueUs == 0) {
        mCreationStampDueUs = nowUs + CREATION_STAMP_DELAY_US;
        return;
    }
    if (nowUs < mCreationStampDueUs) {
        return;
    }
    rtcCreationStampPending = false;
    // Our patched f_utime() sets the created date along with the modified one (tools/patch_fatfs_exfat.py)
    if (utime(mFilename.c_str(), NULL) != 0) {
        printf("%s() ERROR. failed to set the dates of %s, errno=%d (%s) \n", __FUNCTION__, mFilename.c_str(), errno, strerror(errno));
    }
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
    // 100 ms: the old raw 10 ticks were 10 ms at a 1000 Hz tick and are 100 ms at today's 100 Hz,
    // which is what the 100 Hz soak ran with
    if( xSemaphoreTake( mSemaphore, pdMS_TO_TICKS(100) ) == pdTRUE ) {
        ON_SCOPE_EXIT(SemaphoreGive, mSemaphore);
        mDamageHandled = false;
        makeDirectory();
        if (!openFile()) {
            if (!replaceDamagedFile(errno)) {
                printf("%s() ABORT. file handle _log_remote_fp is NULL\n", __FUNCTION__);
                return false;
            }
        }
        // always write a empty line after opening to avoid continuing an old deprecated line
        return _write("\n");
    }
    else {
        printf("%s() ABORT. Semaphore is blocked\n", __FUNCTION__);
        return false;
    }
}

void  RotateFile::close() {
    if( xSemaphoreTake( mSemaphore, portMAX_DELAY ) == pdTRUE ) {
        ON_SCOPE_EXIT(SemaphoreGive, mSemaphore);
        if (!_fp) {
            printf("%s() ABORT. file handle _log_remote_fp is NULL\n", __FUNCTION__);
            return;
        }
        // sync everything to filesystem before cosing the file
        fsync(fileno(_fp));
        fclose(_fp);
        _fp = NULL;
    }
}
bool RotateFile::abandon(uint32_t lockTimeoutMs) {
    if( xSemaphoreTake( mSemaphore, pdMS_TO_TICKS(lockTimeoutMs) ) != pdTRUE ) {
        printf("%s() a writer still holds the log file\n", __FUNCTION__);
        return false;
    }
    ON_SCOPE_EXIT(SemaphoreGive, mSemaphore);
    if (_fp) {
        // No fsync() and no rotate(): the medium is gone. fclose() still has to run to release
        // the FILE object, and its failing flush costs one SDMMC timeout at most.
        fclose(_fp);
        _fp = NULL;
    }
    return true;
}

bool RotateFile::sync(uint32_t lockTimeoutMs) {
    if( xSemaphoreTake( mSemaphore, pdMS_TO_TICKS(lockTimeoutMs) ) != pdTRUE ) {
        printf("%s() a writer still holds the log file\n", __FUNCTION__);
        return false;
    }
    ON_SCOPE_EXIT(SemaphoreGive, mSemaphore);
    return (_fp == NULL) || commit(_fp);
}

bool RotateFile::write(const char *data) {
    if( xSemaphoreTake( mSemaphore, pdMS_TO_TICKS(LOCK_TIMEOUT_MS) ) == pdTRUE ) {
        ON_SCOPE_EXIT(SemaphoreGive, mSemaphore);
        if (_fp == NULL) {
            printf("%s() ABORT. file handle _log_remote_fp is NULL\n", __FUNCTION__);
            return false;
        }
        if (!rotate()) {
            printf("%s() ABORT. Failed to rotate file and open new\n", __FUNCTION__);
            return false;
        }
        return (fprintf(_fp, data) >= 0);
    }
    return true;
}
bool RotateFile::vprintf(const char *fmt, va_list args) { 
    if( xSemaphoreTake( mSemaphore, pdMS_TO_TICKS(LOCK_TIMEOUT_MS) ) == pdTRUE ) {
        ON_SCOPE_EXIT(SemaphoreGive, mSemaphore);
        if (_fp == NULL) {
            printf("%s() ABORT. file handle _log_remote_fp is NULL\n", __FUNCTION__);
            return false;
        }
        if (!rotate()) {
            printf("%s() ABORT. Failed to rotate file and open new\n", __FUNCTION__);
            return false;
        }
        // #1 Write to SPIFFS
        int iresult = vfprintf(_fp, fmt, args);
        if (iresult < 0) {
            const int err = errno;
            printf("%s() failed vfprintf(), errno=%d (%s) \n", __FUNCTION__, err, strerror(err));
            return replaceDamagedFile(err);
        }

        // #2 Smart commit after x writes
        mWriteCounter++;
        if (mWriteCounter % mWriteCacheCycle == 0) {
            /////printf("%s() fsync'ing log file on SPIFFS (WRITE_CACHE_CYCLE=%u)\n", WRITE_CACHE_CYCLE);
            if (!commit(_fp)) {
                const int err = errno;
                printf("%s() failed to commit the file, errno=%d (%s) \n", __FUNCTION__, err, strerror(err));
                return replaceDamagedFile(err);
            }
        }

        // #3 The clock has been set since this file was created on the build date
        if (rtcCreationStampPending && ESP32Time::isClockSet()) {
            stampCreationTime();
        }
    }
    return true; 
}