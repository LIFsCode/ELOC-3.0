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

#include <time.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_spiffs.h"

#include "ffsutils.h"

namespace ffsutil {

bool fileExist(const char* filename) {
    struct stat st;
    return (stat(filename, &st) == 0);
}

long getFileSize(const char* filename)
{
    struct stat stat_buf;
    int rc = stat(filename, &stat_buf);
    return rc == 0 ? stat_buf.st_size : -1;
}


void printSPIFFS_size() {
    printf("-----------------------------------\n");

	uint32_t tot=0, used=0;
    esp_spiffs_info(NULL, &tot, &used);
    printf("SPIFFS: free %d KB of %d KB\n", (tot-used) / 1024, tot / 1024);
    printf("-----------------------------------\n\n");
}

void printListDir(const char *path) {

    DIR *dir = NULL;
    struct dirent *ent;
    char type;
    char size[12];
    char tpath[255];
    char tbuffer[80];
    struct stat sb;
    struct tm *tm_info;
    char *lpath = NULL;
    int statok;

    printf("\nList of Directory [%s]\n", path);
    printf("-----------------------------------\n");
    // Open directory
    dir = opendir(path);
    if (!dir) {
        printf("Error opening directory\n");
        return;
    }

    // Read directory entries
    uint64_t total = 0;
    int nfiles = 0;
    printf("T  Size      Date/Time         Name\n");
    printf("-----------------------------------\n");
    while ((ent = readdir(dir)) != NULL) {
        sprintf(tpath, path);
        if (path[strlen(path)-1] != '/') strcat(tpath,"/");
        strcat(tpath,ent->d_name);
        tbuffer[0] = '\0';

        // Get file stat
        statok = stat(tpath, &sb);

        if (statok == 0) {
            tm_info = localtime(&sb.st_mtime);
            strftime(tbuffer, 80, "%d/%m/%Y %R", tm_info);
        }
        else sprintf(tbuffer, "                ");

        if (ent->d_type == DT_REG) {
            type = 'f';
            nfiles++;
            if (statok) strcpy(size, "       ?");
            else {
                total += sb.st_size;
                if (sb.st_size < (1024*1024)) sprintf(size,"%8d", (int)sb.st_size);
                else if ((sb.st_size/1024) < (1024*1024)) sprintf(size,"%6dKB", (int)(sb.st_size / 1024));
                else sprintf(size,"%6dMB", (int)(sb.st_size / (1024 * 1024)));
            }
        }
        else {
            type = 'd';
            strcpy(size, "       -");
        }

        printf("%c  %s  %s  %s\r\n",
            type,
            size,
            tbuffer,
            ent->d_name
        );
    }
    if (total) {
        printf("-----------------------------------\n");
    	if (total < (1024*1024)) printf("   %8d", (int)total);
    	else if ((total/1024) < (1024*1024)) printf("   %6dKB", (int)(total / 1024));
    	else printf("   %6dMB", (int)(total / (1024 * 1024)));
    	printf(" in %d file(s)\n", nfiles);
    }
    printf("-----------------------------------\n");

    closedir(dir);

    free(lpath);
}

bool folderExists(const char *folder) {
    struct stat sb;
    if (stat(folder, &sb) == 0 && S_ISDIR(sb.st_mode)) {
        return true;
    } else {
        return false;
    }
    return false;
}


sdTestSpeed_t TestSDFile(const char *path, uint8_t *buf, int len, int TEST_FILE_SIZE /*= -1*/)
{
    static const char* TAG = "SD_TEST";
    sdTestSpeed_t result = {0,0,0};

    // First do write benchmark
    result.blockSize = len;
    unsigned long start_time = esp_timer_get_time();
    FILE* file = fopen(path, "w+");
    if (!file) {
        ESP_LOGE(TAG, "Failed to open file for writing");
        return result;
    }
    if (TEST_FILE_SIZE < 0) {
        TEST_FILE_SIZE = len;
    }
    int  loop = TEST_FILE_SIZE / len;
    ESP_LOGI(TAG, "%5d passes @ %2dKB = %d\t",loop,len/1024,TEST_FILE_SIZE);
    while (loop--) {
        if (!fwrite(buf, sizeof(char), len, file)) {
            ESP_LOGE(TAG, "Write failed");
            return result;
        }
        fsync(fileno(file));
    }
    fclose(file);
    unsigned long time_used = (esp_timer_get_time() - start_time)/1000;
    result.writeSpeedKBs = TEST_FILE_SIZE / time_used;
    ESP_LOGI(TAG, "Write: %5lu kB/s",  result.writeSpeedKBs);

    // Secondly do read benchmark
    start_time = esp_timer_get_time();
    file = fopen(path, "r");
    if (!file) {
      ESP_LOGE(TAG, "Failed to open file for reading");
      return result;
    }
    loop = TEST_FILE_SIZE / len;
    while (loop--)  {
        if (!fread(buf, sizeof(char), len, file)) {
          ESP_LOGI(TAG, "Read failed");
          return result;
        }
    }
    fclose(file);
    time_used = (esp_timer_get_time() - start_time)/1000;
    result.readSpeedKBs = TEST_FILE_SIZE / time_used;
    ESP_LOGI(TAG, "Read: %5lu kB/s",  result.readSpeedKBs);
    return result;
}

size_t getFileListWithExtension(const char *path, const char *extension, std::vector<std::string> &files) {
    static const char* TAG = "getFileListWithExtension";

    DIR *dir;
    struct dirent *ent;

    if ((dir = opendir(path)) != NULL) {
        while ((ent = readdir(dir)) != NULL) {
            if (ent->d_type == DT_REG) {
                std::string fname = ent->d_name;
                if (fname.find(extension) != std::string::npos) {
                    ESP_LOGI(TAG, "Found filename: %s", fname.c_str());
                    files.push_back(fname);
                }
            }
        }
        closedir(dir);
    } else {
        return -1;
    }
    return files.size();
}

}  // namespace ffsutil
