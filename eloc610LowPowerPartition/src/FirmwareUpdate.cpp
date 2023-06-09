/*
 * Created on Fri May 05 2023
 *
 * Project: International Elephant Project (Wildlife Conservation International)
 *
 * The MIT License (MIT)
 * Copyright (c) 2023 Fabian Lindner
 * Copyright (c) 2023 tbgilson
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
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>

#include "config.h"

#include "esp_partition.h"
#include "esp_ota_ops.h"
#include "esp_sleep.h"
#include "soc/rtc_wdt.h"

#include <sys\types.h> 
#include <sys\stat.h> 
#include <time.h>
#include <sys/time.h>

static const char *TAG = "UPDATE";

//for update
typedef struct binary_data_t {
    unsigned long size;
    unsigned long remaining_size;
    void *data;
} binary_data_t;

size_t fpread(void *buffer, size_t size, size_t nitems, size_t offset, FILE *fp) {
    if (fseek(fp, offset, SEEK_SET) != 0)
        return 0;
    return fread(buffer, size, nitems, fp);
}

bool success=false;

const esp_partition_t * checkIfCanUpdate(const char *filename, const char *partitionName) {

    long fileDate = 0;
    long partitionDate = 1;
    struct stat st;
    struct tm timestruct;
    esp_app_desc_t app_info;
    const esp_partition_t *thePartition = NULL;

    stat(filename, &st);
    fileDate = st.st_mtim.tv_sec;
    ESP_LOGI(TAG, "%s: File creation date: %ld", filename, st.st_mtim.tv_sec); // tv_sec is a long int

    // high power
     thePartition = esp_ota_get_next_update_partition(NULL);
    // thePartition = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, partitionName);

    ESP_LOGI(TAG, "Update partition: '%s' at offset 0x%" PRIx32 " with size 0x%" PRIx32, thePartition->label, thePartition->address, thePartition->size);
    if (esp_ota_get_partition_description(thePartition, &app_info) == ESP_OK) {
        ESP_LOGI(TAG, "compilation_date:%s", app_info.date);
        ESP_LOGI(TAG, "compilation_time:%s", app_info.time);
    } else {
        ESP_LOGI(TAG, "could not read partition information");
        return thePartition;
    }

    struct tm *timeptr, result;
    char buf[100] = "\0";
    strcat(buf, app_info.date);
    strcat(buf, " ");
    strcat(buf, app_info.time);
    ESP_LOGI(TAG, "buf concatenated %s", buf);
    if (strptime(buf, "%b %d %Y %H:%M:%S", &result) == NULL) {

        printf("\nstrptime failed\n");
        return NULL;
    } else {
        printf("tm_hour:  %d\n", result.tm_hour);
        printf("tm_min:  %d\n", result.tm_min);
        printf("tm_sec:  %d\n", result.tm_sec);
        printf("tm_mon:  %d\n", result.tm_mon);
        printf("tm_mday:  %d\n", result.tm_mday);
        printf("tm_year:  %d\n", result.tm_year);
        printf("tm_yday:  %d\n", result.tm_yday);
        printf("tm_wday:  %d\n", result.tm_wday);

        time_t temp; // time_t is a long in seconds.
        partitionDate = mktime(&result);

        ESP_LOGI(TAG, "partition creation date/time in unix timestamp seconds is  %ld", partitionDate);

        if (partitionDate >= fileDate) {
            ESP_LOGI(TAG, "partitionDateTime is greater or equal to fileDateTime, returning false");
            return NULL;
        } else {
            ESP_LOGI(TAG, "partitionDateTime is less than fileDateTime, returning true");
            return thePartition;
        }
    }
}

static esp_err_t validate_image_header(esp_app_desc_t *new_app_info) {

    // char time[16];              /*!< Compile time */
    // char date[16];              /*!< Compile date*/

    if (new_app_info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_app_desc_t running_app_info;
    if (esp_ota_get_partition_description(running, &running_app_info) == ESP_OK) {
        ESP_LOGI(TAG, "Running firmware version: %s", running_app_info.version);
    }

    return ESP_OK;
}

#define BUFFER_SIZE 2000

void try_update(const esp_partition_t *update_partition, const char *filename) {
    // return;
    esp_ota_handle_t update_handle = 0;
    esp_err_t err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &update_handle);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "esp_begin result = %s", esp_err_to_name(err));
    }
    binary_data_t data;
    FILE *file = fopen(filename, "rb");
    // Get file length
    fseek(file, 0, SEEK_END);
    data.size = ftell(file);
    data.remaining_size = data.size;
    fseek(file, 0, SEEK_SET);
    ESP_LOGI(TAG, "size %lu", data.size);
    data.data = (char *)malloc(BUFFER_SIZE);
    while (data.remaining_size > 0) {
        size_t size = data.remaining_size <= BUFFER_SIZE ? data.remaining_size : BUFFER_SIZE;
        fpread(data.data, size, 1, data.size - data.remaining_size, file);
        err = esp_ota_write(update_handle, data.data, size);
        if (data.remaining_size <= BUFFER_SIZE) {
            break;
        }
        data.remaining_size -= BUFFER_SIZE;
    }

    ESP_LOGI(TAG, "Ota result = %d", err);
    err = esp_ota_end(update_handle);
    if (err != ESP_OK) {
        if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
            ESP_LOGE(TAG, "Image validation failed, image is corrupted");
        }
        ESP_LOGE(TAG, "esp_ota_end failed (%s)!", esp_err_to_name(err));
    } else {
        err = esp_ota_set_boot_partition(update_partition);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "esp_ota_set_boot_partition failed (%s)!", esp_err_to_name(err));
            return;
        }
        ESP_LOGI(TAG, "update success.");
        success = true;
    }
}

static void LEDflashError() {
    ESP_LOGI(TAG, "-----fast flash------------");
    for (int i=0;i<10;i++){
        gpio_set_level(STATUS_LED, 1);
        vTaskDelay(pdMS_TO_TICKS(40));
        gpio_set_level(STATUS_LED, 0);
        vTaskDelay(pdMS_TO_TICKS(40));
    }  
}

bool updateFirmware() {

    FILE *f = fopen("/sdcard/eloc/update/elocupdate.bin", "r");

    if (f == NULL) {
        fclose(f);
        ESP_LOGI(TAG, "No update file found in /sdcard/eloc/update");
    } else {
        fclose(f);
        ESP_LOGI(TAG, "Trying to update ELOC Firmware");
        const esp_partition_t *updatePartition = checkIfCanUpdate("/sdcard/eloc/update/elocupdate.bin", "partition1");
        if (updatePartition != NULL) {
        }
        try_update(updatePartition, "/sdcard/eloc/update/elocupdate.bin");
    }
    if (success) {
        gpio_set_level(STATUS_LED, 1);
        vTaskDelay(pdMS_TO_TICKS(3000));
        gpio_set_level(STATUS_LED, 0);

        ESP_LOGI(TAG, "Prepare to restart system!");
        esp_restart();

        return true;

    } else {
        ESP_LOGI(TAG, "No update was performed");
        LEDflashError();
        return false;
    }
}