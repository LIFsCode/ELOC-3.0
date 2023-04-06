
//#include "esp_err.h"
#include "esp_log.h"
//#include "esp_pm.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>

#include "SDCardSDIO.h"

#include "SDCard.h"
//#include "SPIFFS.h"

//#include "WAVFileReader.h"
#include "config.h"


#include "esp_partition.h"
#include "esp_ota_ops.h"
#include "esp_sleep.h"
#include "soc/rtc_wdt.h"

#include <sys\types.h> 
#include <sys\stat.h> 
#include <time.h>
#include <sys/time.h>

static const char *TAG = "main";
bool gMountedSDCard=false;

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



// idf-wav-sdcard/lib/sd_card/src/SDCard.cpp   m_host.max_freq_khz = 18000;
// https://github.com/atomic14/esp32_sdcard_audio/commit/000691f9fca074c69e5bb4fdf39635ccc5f993d4#diff-6410806aa37281ef7c073550a4065903dafce607a78dc0d4cbc72cf50bac3439

extern "C"
{
  void app_main(void);
}

#ifdef USE_SPI_VERSION
    SDCard *theSDCardObject;
 #endif

#ifdef USE_SDIO_VERSION
    SDCardSDIO *theSDCardObject;
 #endif


void resetESP32() {
        
      //delete theSDCardObject;
      //gMountedSDCard=false;
     

    
    ESP_LOGI(TAG, "Resetting in 100 ms");
    rtc_wdt_protect_off();      //Disable RTC WDT write protection
    
    rtc_wdt_set_stage(RTC_WDT_STAGE0, RTC_WDT_STAGE_ACTION_RESET_SYSTEM);
    rtc_wdt_set_time(RTC_WDT_STAGE0, 100);
    rtc_wdt_enable();           //Start the RTC WDT timer
    rtc_wdt_protect_on();       //Enable RTC WDT write protection
    vTaskDelay(pdMS_TO_TICKS(500));


}



void printFileCreationDateTime(const char * thefile) {
    struct stat st;
    stat(thefile, &st);
    struct tm timestruct;
    //timespec thetime;
    //thetime.tv_sec;
     ESP_LOGI(TAG, "File creation date: %ld", st.st_mtim.tv_sec );  //tv_sec is a long int

}



bool checkIfCanUpdate(const char * filename, const char *partitionName) {
    
    long fileDate=0;
    long  partitionDate=1;
    struct stat st;
    struct tm timestruct;
    esp_app_desc_t app_info;
     const esp_partition_t *thePartition;

    stat(filename, &st);
    fileDate=st.st_mtim.tv_sec;
    ESP_LOGI(TAG, "File creation date: %ld", st.st_mtim.tv_sec );  //tv_sec is a long int




 
   
      // high power
      //update_partition = esp_ota_get_next_update_partition(NULL);
       thePartition=esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, partitionName);
 
      if (esp_ota_get_partition_description(thePartition, &app_info) == ESP_OK)
      {
          ESP_LOGI(TAG, "compilation_date:%s", app_info.date);
          ESP_LOGI(TAG, "compilation_time:%s", app_info.time);
      } else {
          ESP_LOGI(TAG,"could not read partition information");
      }

   struct tm *timeptr,result;
   char buf[100]="\0";
   strcat(buf, app_info.date);
   strcat(buf, " ");
   strcat(buf, app_info.time);
   ESP_LOGI(TAG, "buf concatenated %s", buf ); 
  if (strptime(buf, "%b %d %Y %H:%M:%S",&result) == NULL) {
      
          printf("\nstrptime failed\n");
          return false;
  } else
    {
          printf("tm_hour:  %d\n",result.tm_hour);
          printf("tm_min:  %d\n",result.tm_min);
          printf("tm_sec:  %d\n",result.tm_sec);
          printf("tm_mon:  %d\n",result.tm_mon);
          printf("tm_mday:  %d\n",result.tm_mday);
          printf("tm_year:  %d\n",result.tm_year);
          printf("tm_yday:  %d\n",result.tm_yday);
          printf("tm_wday:  %d\n",result.tm_wday);


            time_t temp; //time_t is a long in seconds.
            partitionDate=mktime(&result);
          
             ESP_LOGI(TAG, "partition creation date/time in unix timestamp seconds is  %ld", partitionDate ); 

             if (partitionDate >= fileDate) {
                ESP_LOGI(TAG, "partitionDateTime is greater or equal to fileDateTime, returning false" ); 
                return false ;
             } else {
                ESP_LOGI(TAG, "partitionDateTime is less than fileDateTime, returning true" ); 
                return true;
             }
    }
  
    
    





}







static esp_err_t validate_image_header(esp_app_desc_t *new_app_info)
{
    
    //char time[16];              /*!< Compile time */
    //char date[16];              /*!< Compile date*/
    
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

void try_update(int partitionNumber ,const char *filename) {
    //return;
    esp_ota_handle_t update_handle = 0;
    const esp_partition_t *update_partition;
    if (partitionNumber==0) {
      // high power
      //update_partition = esp_ota_get_next_update_partition(NULL);
        update_partition=esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, "partition0");
     } else {
        //low power
        update_partition=esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, "partition1");
 
     }
    esp_err_t err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &update_handle);
    ESP_LOGI(TAG, "esp_begin result = %d", err);
    binary_data_t data;
    FILE *file = fopen(filename, "rb");
    //Get file length
    fseek(file, 0, SEEK_END);
    data.size = ftell(file);
    data.remaining_size = data.size;
    fseek(file, 0, SEEK_SET);
    ESP_LOGI(TAG, "size %lu", data.size);
    data.data = (char *) malloc(BUFFER_SIZE);
    while (data.remaining_size > 0) {
        size_t size = data.remaining_size <= BUFFER_SIZE ? data.remaining_size : BUFFER_SIZE;
        fpread(data.data, size, 1,
               data.size - data.remaining_size, file);
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
         ESP_LOGI(TAG, "update success.");
        success=true;
    }

   
}







void LEDflashError() {
      ESP_LOGI(TAG, "-----fast flash------------");
      for (int i=0;i<10;i++){
       gpio_set_level(STATUS_LED, 1);
         vTaskDelay(pdMS_TO_TICKS(40));
        gpio_set_level(STATUS_LED, 0);
         vTaskDelay(pdMS_TO_TICKS(40));
    }  
}


void changeBootPartition() {  ///will always set boot partition back to partition0
    
    
    const esp_partition_t *partition = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, "partition0");
    ESP_LOGI(TAG, "%d  address", partition->address);
    
    esp_ota_set_boot_partition(partition);
     ESP_LOGI(TAG, "changed boot partition to  partition0");
  


}









void mountSDCard() {
    gMountedSDCard=false;
    #ifdef USE_SPI_VERSION
  
 
          ESP_LOGI(TAG, "TRYING to mount SDCArd, SPI ");
           theSDCardObject= new SDCard("/sdcard",PIN_NUM_MISO, PIN_NUM_MOSI, PIN_NUM_CLK, PIN_NUM_CS);


    #endif 
    
    #ifdef USE_SDIO_VERSION

          ESP_LOGI(TAG, "TRYING to mount SDCArd, SDIO ");
          theSDCardObject= new SDCardSDIO("/sdcard");
    #endif



}





void app_main(void)
{
  changeBootPartition(); // so if reboots, always boot into the bluetooth partition
  ESP_LOGI(TAG, "\n\n---------UPDATE VERSION %s\n\n", VERSION);
 
 

 #ifdef USE_SPI_VERSION
      gpio_set_direction(STATUS_LED, GPIO_MODE_OUTPUT);
      gpio_set_direction(BATTERY_LED, GPIO_MODE_OUTPUT);

      gpio_set_direction(PIN_NUM_MISO, GPIO_MODE_INPUT);
      gpio_set_pull_mode(PIN_NUM_MISO, GPIO_PULLUP_ONLY);
      
      gpio_set_direction(PIN_NUM_CLK, GPIO_MODE_INPUT);
      gpio_set_pull_mode(PIN_NUM_CLK, GPIO_PULLUP_ONLY);

      gpio_set_direction(PIN_NUM_MOSI, GPIO_MODE_INPUT);
      gpio_set_pull_mode(PIN_NUM_MOSI, GPIO_PULLUP_ONLY);

      gpio_set_direction(PIN_NUM_CS, GPIO_MODE_INPUT);
      gpio_set_pull_mode(PIN_NUM_CS, GPIO_PULLUP_ONLY);
      
      gpio_sleep_sel_dis(GPIO_BUTTON);
      gpio_sleep_sel_dis(OTHER_GPIO_BUTTON);
      gpio_sleep_sel_dis(PIN_NUM_MISO);
      gpio_sleep_sel_dis(PIN_NUM_CLK);
      gpio_sleep_sel_dis(PIN_NUM_MOSI);
      gpio_sleep_sel_dis(PIN_NUM_CS);
      gpio_sleep_sel_dis(I2S_MIC_SERIAL_CLOCK);
      gpio_sleep_sel_dis(I2S_MIC_LEFT_RIGHT_CLOCK);
      gpio_sleep_sel_dis(I2S_MIC_SERIAL_DATA);

     gpio_set_intr_type(OTHER_GPIO_BUTTON, GPIO_INTR_POSEDGE);
     gpio_set_intr_type(GPIO_BUTTON, GPIO_INTR_POSEDGE);


#endif

  #ifdef USE_SDIO_VERSION
      gpio_set_direction(STATUS_LED, GPIO_MODE_OUTPUT);
      gpio_set_direction(BATTERY_LED, GPIO_MODE_OUTPUT);

  
      
      gpio_sleep_sel_dis(GPIO_BUTTON);
      gpio_sleep_sel_dis(OTHER_GPIO_BUTTON);
      gpio_sleep_sel_dis(I2S_MIC_SERIAL_CLOCK);
      gpio_sleep_sel_dis(I2S_MIC_LEFT_RIGHT_CLOCK);
      gpio_sleep_sel_dis(I2S_MIC_SERIAL_DATA);

      gpio_set_intr_type(GPIO_BUTTON, GPIO_INTR_POSEDGE);
      gpio_set_intr_type(OTHER_GPIO_BUTTON, GPIO_INTR_POSEDGE);

#endif




  gpio_set_level(STATUS_LED, 0);
  gpio_set_level(BATTERY_LED, 0);
 
 //heap_caps_dump_all();
 
  //char *sessionFolder;
  //spiffs will have already been deleted
  
    


   
 


 



  while (true)
  {
    // wait for the user to push and hold the button
    //wait_for_button_push();

  
  FILE *f;
  
  mountSDCard();
      


 
 
 
    
    if (gMountedSDCard) {
        //
        /////////////////////////////LOW POWER /////////////////////////////////////////
        #ifdef USE_SPI_VERSION
        f = fopen("/sdcard/eloc/update/elocupdatespilowpower.bin", "r");
        #endif
        #ifdef USE_SDIO_VERSION
        f = fopen("/sdcard/eloc/update/elocupdatesdiolowpower.bin", "r");
        #endif


        if (f == NULL) {
            fclose(f);
             ESP_LOGI(TAG, "No lowPower update file found in /sdcard/eloc/update");




        } else {
                fclose(f);
                //printFileCreationDateTime("/sdcard/eloc/update/elocupdatespilowpower.bin");
                //printPartitionCreationDate("partition1");
                #ifdef USE_SPI_VERSION
                    ESP_LOGI(TAG, "Trying to update SPI Low Power Partion1");
                    if (checkIfCanUpdate("/sdcard/eloc/update/elocupdatespilowpower.bin", "partition1")) {
                       try_update(1, "/sdcard/eloc/update/elocupdatespilowpower.bin");
                    }
                #endif
                #ifdef USE_SDIO_VERSION
                  ESP_LOGI(TAG, "Trying to update SDIO Low Power Partion1");
                  if (checkIfCanUpdate("/sdcard/eloc/update/elocupdatesdiolowpower.bin", "partition1")) {
                    try_update(1, "/sdcard/eloc/update/elocupdatesdiolowpower.bin");
                  }
                #endif

        }





          /////////////////////////////HIGH_POWER/////////////////////////////////////////

        #ifdef USE_SPI_VERSION
        f = fopen("/sdcard/eloc/update/elocupdatespihighpower.bin", "r");
        #endif
        #ifdef USE_SDIO_VERSION
        f = fopen("/sdcard/eloc/update/elocupdatesdiohighpower.bin", "r");
        #endif


        if (f == NULL) {
            fclose(f);
             ESP_LOGI(TAG, "No highPower update file found in /sdcard/eloc/update. ");




        } else {
                fclose(f);

                #ifdef USE_SPI_VERSION
                    ESP_LOGI(TAG, "Trying to update SPI High Power Partion0");
                     if (checkIfCanUpdate( "/sdcard/eloc/update/elocupdatespihighpower.bin", "partition0")) { 
                     try_update(0, "/sdcard/eloc/update/elocupdatespihighpower.bin");
                   }
                #endif
                #ifdef USE_SDIO_VERSION
                  ESP_LOGI(TAG, "Trying to update SDIO High Power Partion1");
                  if (checkIfCanUpdate("/sdcard/eloc/update/elocupdatesdiohighpower.bin", "partition0")) {
                    try_update(0, "/sdcard/eloc/update/elocupdatesdiohighpower.bin");
                  }
                #endif

        }











    } else {
      ESP_LOGI(TAG, "Error in Update. Could not mount SD Card");

    
   
  } 
  if (success) {
        gpio_set_level(STATUS_LED, 1);
         vTaskDelay(pdMS_TO_TICKS(3000));
          gpio_set_level(STATUS_LED, 0);

  } else {
      ESP_LOGI(TAG, "No update was performed");
    LEDflashError();
  }
  
  delete  theSDCardObject;
  resetESP32();

  }

   
}
