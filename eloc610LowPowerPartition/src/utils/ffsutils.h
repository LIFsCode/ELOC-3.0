/*
 * Created on Fri May 05 2023
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

#ifndef UTILS_FFSUTILS_H_
#define UTILS_FFSUTILS_H_

namespace ffsutil {

/// @brief Prints a list of files & subdirectories with sizes of a given path
/// @param path filesystem directory which needs to be printed
void printListDir(const char *path);

void printSPIFFS_size();

bool fileExist(const char* filename);

long getFileSize(const char* filename);

bool folderExists(const char* folder);

typedef struct {
    unsigned long blockSize;
    unsigned long writeSpeedKBs;
    unsigned long readSpeedKBs;
}sdTestSpeed_t;
sdTestSpeed_t TestSDFile(const char *path, uint8_t *buf, int len, int TEST_FILE_SIZE = -1);

}


#endif // UTILS_FFSUTILS_H_