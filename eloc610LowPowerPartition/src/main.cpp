
#include "esp_err.h"
#include "esp_log.h"
#include <rom/ets_sys.h>
#include "esp_pm.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_task_wdt.h"
#include "I2SSampler.h"
#include "I2SMEMSSampler.h"
//#include  "periph_ctrl.h"
//#include "I2SOutput.h"
#include "SDCardSDIO.h"

#include "SDCard.h"
#include "SPIFFS.h"
#include <FFat.h>
#include <ff.h>
#include "WAVFileWriter.h"
//#include "WAVFileReader.h"
#include "config.h"
#include <string.h>

#include "esp_partition.h"
#include "esp_ota_ops.h"

#include <time.h>
#include <sys/time.h>
#include <sys/stat.h>

#include "esp_sleep.h"
#include "rtc_wdt.h"
#include <driver/rtc_io.h>
//#include "soc/efuse_reg.h"

/** Arduino libraries*/
#include "ESP32Time.h"
#include "BluetoothSerial.h"
/** Arduino libraries END*/


#include "version.h"

#include "utils/ffsutils.h"
#include "lis3dh.h"
#include "Battery.hpp"
#include "ElocSystem.hpp"
#include "ElocConfig.hpp"
#include "ElocStatus.hpp"
#include "utils/logging.hpp"
#include "Commands/BluetoothServer.hpp"
#include "FirmwareUpdate.hpp"
#include "PerfMonitor.hpp"

static const char *TAG = "main";


bool gMountedSDCard=false;

int32_t *graw_samples;

int gRawSampleBlockSize=1000;
int gSampleBlockSize=16000; //must be factor of 1000   in samples
int gBufferLen=I2S_DMA_BUFFER_LEN; //in samples
int gBufferCount=I2S_DMA_BUFFER_COUNT;   // so 6*4000 = 24k buf len
//always keep these in same order


////// these are the things you can change for now. 

uint32_t  gRealSampleRate;
uint64_t gStartupTime; //gets read in at startup to set tystem time. 

char gSessionFolder[65];

int gMinutesWaitUntilDeepSleep=60; //change to 1 or 2 for testing

ESP32Time timeObject;
//WebServer server(80);
//bool updateFinished=false;
bool gWillUpdate=false;

//String gTimeDifferenceCode; //see getTimeDifferenceCode() below

 #ifdef USE_SPI_VERSION
    SDCard *theSDCardObject;
 #endif

#ifdef USE_SDIO_VERSION
    SDCardSDIO *theSDCardObject;
 #endif


void writeSettings(String settings);
void doDeepSleep();
void setTime(long epoch, int ms);

// idf-wav-sdcard/lib/sd_card/src/SDCard.cpp   m_host.max_freq_khz = 18000;
// https://github.com/atomic14/esp32_sdcard_audio/commit/000691f9fca074c69e5bb4fdf39635ccc5f993d4#diff-6410806aa37281ef7c073550a4065903dafce607a78dc0d4cbc72cf50bac3439

extern "C"
{
  void app_main(void);
}


void resetPeripherals() {
   /*
   periph_module_reset( PERIPH_I2S0_MODULE);  
    periph_module_reset( PERIPH_BT_MODULE);
    periph_module_reset( PERIPH_WIFI_BT_COMMON_MODULE);
    periph_module_reset( PERIPH_BT_MODULE);
    periph_module_reset(  PERIPH_BT_BASEBAND_MODULE);
    periph_module_reset(PERIPH_BT_LC_MODULE);
    //periph_module_reset(PERIPH_UART0_MODULE);    //this used by debugger?
    periph_module_reset(PERIPH_UART1_MODULE);  //this was it! bug stops bluetooth 
                                               //from re-broadcasting in other partition on  https://github.com/espressif/esp-idf/issues/9971

    periph_module_reset(PERIPH_UART2_MODULE); //just in case
    
   */

}


void printRevision() {

  /*
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    printf("\nThis is ESP32 chip with %d CPU cores, WiFi%s%s, ",
            chip_info.cores,
            (chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "",
            (chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "");

    printf("silicon revision %d, ", chip_info.revision);

    printf("%dMB %s flash\n", spi_flash_get_chip_size() / (1024 * 1024),
            (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");
    */
}

void delay (int  ms) {

  vTaskDelay(pdMS_TO_TICKS(ms));


} 


void testInput() {
          I2SSampler *input;
          for (uint32_t i=1000; i<34000; i=i+2000) {
                 i2s_mic_Config.sample_rate=i;
                 i2s_mic_Config.use_apll=getMicInfo().MicUseAPLL;
           

                 input = new I2SMEMSSampler(I2S_NUM_0, i2s_mic_pins, i2s_mic_Config, getMicInfo().MicBitShift, getConfig().listenOnly, getMicInfo().MicUseTimingFix);
                 input->start();
                 delay(100);
                  ESP_LOGI(TAG, "Clockrate: %f", i2s_get_clk(I2S_NUM_0));
                 input->stop();
                  delay(100);
                  //delete input;
                  //delay(100);

        }
        //delete input;
}




void resetESP32() {
      
   
     

    
    ESP_LOGI(TAG, "Resetting in some ms");
    resetPeripherals();
    // esp_task_wdt_init(30, false); //bump it up to 30 econds doesn't work. 
    rtc_wdt_protect_off();      //Disable RTC WDT write protection
    
    rtc_wdt_set_stage(RTC_WDT_STAGE0, RTC_WDT_STAGE_ACTION_RESET_RTC);
    rtc_wdt_set_time(RTC_WDT_STAGE0, 500);
    rtc_wdt_enable();           //Start the RTC WDT timer
    rtc_wdt_protect_on();       //Enable RTC WDT write protection
    
    // ESP_LOGI(TAG, "starting deep sleep");
    //delay(50);
    //esp_deep_sleep_start();
    //ESP_LOGI(TAG, "deep sleep should never make it here");
    
    delay(1000); //should never get here



}



void printMemory() {

      ESP_LOGI(TAG, "\n\n\n\n");
      ESP_LOGI(TAG, "Total Free mem default %d", heap_caps_get_free_size(MALLOC_CAP_DEFAULT));
      ESP_LOGI(TAG, "Largest Block default %d", heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT ));

      ESP_LOGI(TAG, "Total Free mem DMA %d", heap_caps_get_free_size(MALLOC_CAP_DMA));
      ESP_LOGI(TAG, "Largest Block DMA %d", heap_caps_get_largest_free_block(MALLOC_CAP_DMA ));

      ESP_LOGI(TAG, "Total Free mem IRAM %d", heap_caps_get_free_size(MALLOC_CAP_IRAM_8BIT));
      ESP_LOGI(TAG, "Largest Block IRAM %d", heap_caps_get_largest_free_block(MALLOC_CAP_IRAM_8BIT));

      ESP_LOGI(TAG, "Total Free mem EXEC %d", heap_caps_get_free_size(MALLOC_CAP_EXEC));
      ESP_LOGI(TAG, "Largest Block EXEC %d", heap_caps_get_largest_free_block(MALLOC_CAP_EXEC ));

      //heap_caps_dump_all();
      ESP_LOGI(TAG, "\n\n\n\n");


}



int64_t getSystemTimeMS() {
             struct timeval tv_now;
            gettimeofday(&tv_now, NULL);
            int64_t time_us = (     (int64_t)tv_now.tv_sec      * 1000000L) + (int64_t)tv_now.tv_usec;
            time_us=time_us/1000;
          return(time_us);


}



QueueHandle_t rec_req_evt_queue = NULL;

static  void IRAM_ATTR buttonISR(void *args) {
  //return;
  //ESP_LOGI(TAG, "button pressed");
  //delay(5000);
  rec_req_t rec_req = (gRecording != RecState::IDLE) ? REC_REQ_STOP : REC_REQ_START;
  //ets_printf("button pressed");
  xQueueSendFromISR(rec_req_evt_queue, &rec_req, (TickType_t)0);
     
     //detachInterrupt(GPIO_BUTTON);

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


void printPartitionInfo() {  ///will always set boot partition back to partition0 except if it 
    
    
    esp_partition_iterator_t it;

    ESP_LOGI(TAG, "Iterating through app partitions...");
    it = esp_partition_find(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, NULL);

    // Loop through all matching partitions, in this case, all with the type 'data' until partition with desired
    // label is found. Verify if its the same instance as the one found before.
    for (; it != NULL; it = esp_partition_next(it)) {
        const esp_partition_t *part = esp_partition_get(it);
        ESP_LOGI(TAG, "\tfound partition '%s' at offset 0x%" PRIx32 " with size 0x%" PRIx32, part->label, part->address, part->size);
    }
    // Release the partition iterator to release memory allocated for it
    esp_partition_iterator_release(it);

    ESP_LOGI(TAG, "Currently running partitions: ");
    const esp_partition_t *running = esp_ota_get_running_partition();
    ESP_LOGI(TAG, "\t '%s' at offset 0x%" PRIx32 " with size 0x%" PRIx32, running->label, running->address, running->size);
    esp_app_desc_t running_app_info;
    if (esp_ota_get_partition_description(running, &running_app_info) == ESP_OK) {
        ESP_LOGI(TAG, "Running firmware version: %s", running_app_info.version);
    }

}










void time() {

        struct timeval tv_now;
      gettimeofday(&tv_now, NULL);
      int64_t time_us = (     (int64_t)tv_now.tv_sec      * 1000000L) + (int64_t)tv_now.tv_usec;
      time_us=time_us/1000;
   
}

String uint64ToString(uint64_t input) {
  String result = "";
  uint8_t base = 10;

  do {
    char c = input % base;
    input /= base;

    if (c < 10)
      c +='0';
    else
      c += 'A' - 10;
    result = c + result;
  } while (input);
  return result;
}




void setTime(long epoch, int ms) {
  /*

  setTime(atol(seconds.c_str())+(TIMEZONE_OFFSET*60L*60L),  (atol(milliseconds.c_str()))*1000    );
                  //timeObject.setTime(atol(seconds.c_str()),  (atol(milliseconds.c_str()))*1000    );
                  // timestamps coming in from android are always GMT (minus 7 hrs)
                  // if I not add timezone then timeobject is off 
                  // so timeobject does not seem to be adding timezone to system time.
                  // timestamps are in gmt+0, so timestamp convrters
                
                  struct timeval tv_now;
                  gettimeofday(&tv_now, NULL);
                  int64_t time_us = (     (int64_t)tv_now.tv_sec      * 1000000L) + (int64_t)tv_now.tv_usec;
                  time_us=time_us/1000;
  */
  
  
  struct timeval tv;
  tv.tv_sec = epoch;  // epoch time (seconds)
  tv.tv_usec = ms;    // microseconds
  settimeofday(&tv, NULL);
}

// set initial time. If time is not set the getLocalTime() will stuck for 5 ms due to invalid timestamp
void initTime() {
    struct tm tm;
    strptime(BUILDDATE, "%b %d %Y %H:%M:%S %Y", &tm);
    time_t timeSinceEpoch = mktime(&tm);
    timeObject.setTime(timeSinceEpoch);
    ESP_LOGI(TAG, "Setting initial time to build date: %s", BUILDDATE);
}

bool createSessionFolder () {
    String fname;
    //TODO: check if another session identifier based on ISO time for mat would be more helpful
    gSessionIdentifier = getDeviceInfo().fileHeader + uint64ToString(getSystemTimeMS());
    fname = "/sdcard/eloc/" + gSessionIdentifier;
    ESP_LOGI(TAG, "Creating session folder %s", fname.c_str());
    mkdir(fname.c_str(), 0777);

    String cfg;
    printConfig(cfg);
    ESP_LOGI(TAG, "Starting session with this config:\n: %s", cfg.c_str());
    fname += "/" + gSessionIdentifier + ".config";
    FILE *f = fopen(fname.c_str(), "w+");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open config file for storage %s!", fname.c_str());
        return false;
    } 
    fprintf(f, "%s", cfg.c_str());
    fclose(f);
    return true;
}

bool createFilename(char *fname, size_t size) {

    char timeStr[64] = {};
    tm timeinfo = timeObject.getTimeStruct();
    strftime(timeStr, sizeof(timeStr), "%F_%H_%M_%S", &timeinfo);
    fname[0] = '\0';
    int n= snprintf(fname, size, "/sdcard/eloc/%s/%s_%s.wav", gSessionIdentifier.c_str(), gSessionIdentifier.c_str(), timeStr);
    if ((n<0) || (n > size)) {
        ESP_LOGE(TAG, "filename buffer too small");
        return false;
    }
    ESP_LOGI(TAG, "filename %s", fname);
    return true;
}

String getProperDateTime() {

        String year = String(timeObject.getYear()); 
        String month = String(timeObject.getMonth());
        String day = String(timeObject.getDay());
        String hour = String(timeObject.getHour(true));
        String minute = String(timeObject.getMinute());
        String second = String(timeObject.getSecond());
        //String millis = String(timeObject.getMillis());
        if (month.length()==1) month="0"+month;
        if (day.length()==1) day="0"+day;
        if (hour.length()==1) hour="0"+hour;
        if (minute.length()==1) minute="0"+minute;
        if (second.length()==1) second="0"+second;

       return(year+"-"+month+"-"+day+" "+hour+":"+minute+":"+second);

}

void doDeepSleep(){
   

      esp_sleep_enable_ext0_wakeup(GPIO_BUTTON, 0); //try commenting this out
     
      
     // printf("Going to sleep now");
      delay(2000);
      esp_deep_sleep_start(); //change to deep?  
      
      //printf("OK button was pressed.waking up");
      delay(2000);
      esp_restart();


}

void record(I2SSampler *input) {
  
  gRecording=RecState::RECORDING;
  char fname[100];
  
    input->start();
  gRealSampleRate=(int32_t)(i2s_get_clk(I2S_NUM_0));
  ESP_LOGI(TAG, "I2s REAL clockrate in record  %u", gRealSampleRate  );

  
  int64_t recordStartTime= esp_timer_get_time();
    bool deepSleep=false;
  float loops;
  int samples_read;
  int64_t longestWriteTimeMillis=0;
  int64_t writeTimeMillis=0;
  int64_t bufferUnderruns=0;
  float bufferTimeMillis=(((float)gBufferCount*(float)gBufferLen)/(float)getMicInfo().MicSampleRate)*1000;
  int64_t writestart,writeend,loopstart,looptime,temptime;
  
  //BUGME: this should return the session folder name to parse it to createFilename()
  createSessionFolder();

  

  graw_samples = (int32_t *)malloc(sizeof(int32_t) * 1000);
  int16_t *samples = (int16_t *)malloc(sizeof(int16_t) *gSampleBlockSize);
 
 
  
  FILE *fp=NULL;
  WAVFileWriter *writer=NULL;

 
 
  bool stopit = false;
  

  
  if (getConfig().listenOnly)  ESP_LOGI(TAG, "Listening only...");
  while (!stopit) {
      
      
      if (!getConfig().listenOnly) {
            createFilename(fname, sizeof(fname));
            fp = fopen(fname, "wb");

          
            writer = new WAVFileWriter(fp, gRealSampleRate, NUMBER_OF_CHANNELS);
      }
      long loopCounter=0;
      int64_t recordupdate = esp_timer_get_time();
 
 
 
      loops= (float)getConfig().secondsPerFile/((gSampleBlockSize)/((float)getMicInfo().MicSampleRate)) ;
      //printf("loops: "); printf(loops); 
      loopstart=esp_timer_get_time(); 
      while (loopCounter < loops ) {
            loopCounter++;
            temptime=esp_timer_get_time();
            looptime=(temptime-loopstart);
            loopstart=temptime;

           // gpio_set_level(STATUS_LED, 1); //so status led only goes low if we go into light sleep manually.
            //esp_sleep_enable_timer_wakeup(1500000);  //1.5 sec
            //esp_light_sleep_start();
            samples_read=0;
            samples_read = input->read(samples, gSampleBlockSize); //8khz 16k samples so 2 sec
            //delay(3000); //3 secs this delay will not get anything from i2s logging. DELAY is turning off the CPU?
                         // if i use delay I dont get the i2s buffer overflow error. do I ever get it?
                         // fixed if I don't read anything I get watchdog timer errors. 
                         // looks like I am getting the i2s errors now. 
                         //  get the i2s buffer overflow ALWAYS
                         // there is no i2s output when DELAY is active. but get buffer overflow always
            
            gSessionRecordTime=esp_timer_get_time()-recordStartTime;
             if (!getConfig().listenOnly)  {
                  writestart = esp_timer_get_time();
                  writer->write(samples, samples_read);
                  
                  writeend = esp_timer_get_time();
                  writeTimeMillis=(writeend - writestart)/1000;
                  if (writeTimeMillis > longestWriteTimeMillis) longestWriteTimeMillis=writeTimeMillis;
                  if (writeTimeMillis>bufferTimeMillis) bufferUnderruns++;
                  ESP_LOGI(TAG, "Wrote %d samples in %lld ms. Longest: %lld. buffer (ms): %f underrun: %lld loop:%lld",   samples_read,writeTimeMillis,longestWriteTimeMillis,bufferTimeMillis,bufferUnderruns,looptime/1000);
            }
            //if (getConfig().listenOnly)  ESP_LOGI(TAG, "Listening only...");
            rec_req_t rec_req = REC_REQ_NONE;
            if (xQueueReceive(rec_req_evt_queue, &rec_req, 0))  { // 0 waiting time
                if (rec_req == REC_REQ_STOP) {
                  stopit=true; 
                  loopCounter=10000001L; 
                  ESP_LOGI(TAG,"stop recording requested");
                }
                else {
                  ESP_LOGI(TAG,"REC_REQ = %d", rec_req);
                }
            }

            if ((esp_timer_get_time() - recordupdate  ) > 9000000 ) { //9 seconds for the next loop
                ESP_LOGI(TAG,"record in second loop--voltage check");
                //esp_pm_dump_locks(stdout);
                //vTaskGetRunTimeStats( ( char * ) sBuffer );
                //ESP_LOGI(TAG,"vTaskGetRunTimeStats:\n %s", sBuffer);
             
                //printf("freeSpaceGB "+String(gFreeSpaceGB));

                recordupdate=esp_timer_get_time(); 

                //if (gGPIOButtonPressed)  {stopit=true; loopCounter=10000001L; ESP_LOGI(TAG,"gpio button pressed");}
               // ok fix me put me back in if (gFreeSpaceGB<0.2f)  {stopit=true; loopCounter=10000001L; }
                
                 //voltage check
                if ((loopCounter % 50)==0 ) {
                   ESP_LOGI(TAG, "Checking battery state" );
                   Battery::GetInstance().getVoltage();
                   ESP_LOGI(TAG, "Battery: Voltage: %.3fV, %.0f%% SoC", Battery::GetInstance().getVoltage(), Battery::GetInstance().getSoC());
                   if ((Battery::GetInstance().isEmpty())) {
                     stopit=true; loopCounter=10000001L;
                     ESP_LOGI(TAG, "Voltage LOW-OFF. Stopping record. " );
                     deepSleep=true;

                     
                   }   
                } 

               
    
              }
  
          
      }
    
    if (!getConfig().listenOnly) {
        writer->finish();
        fclose(fp);
        delete writer;
    }
    

  }
   ESP_LOGI(TAG, "Stopping record. " );
  
  input->stop();
  //delete input;
  free(samples);
  free(graw_samples);
  
  gSessionRecordTime=esp_timer_get_time()-recordStartTime;
  //int64_t recordEndTime= esp_timer_get_time();
  gTotalRecordTimeSinceReboot=gTotalRecordTimeSinceReboot+gSessionRecordTime; 
  ESP_LOGI(TAG, "total record time since boot = %s sec", uint64ToString(gTotalRecordTimeSinceReboot/1000/1000).c_str());
 

  ESP_LOGI(TAG, "Finished recording");
  //btwrite("Finished recording");
   gRecording=RecState::IDLE;
   gSessionRecordTime=0;
 
   if (deepSleep) doDeepSleep();

  

}




bool mountSDCard() {
    #ifdef USE_SPI_VERSION
          ESP_LOGI(TAG, "TRYING to mount SDCArd, SPI ");
          theSDCardObject = new SDCard("/sdcard",PIN_NUM_MISO, PIN_NUM_MOSI, PIN_NUM_CLK, PIN_NUM_CS);
    #endif 
    
    #ifdef USE_SDIO_VERSION

        ESP_LOGI(TAG, "TRYING to mount SDCArd, SDIO ");
        theSDCardObject = new SDCardSDIO("/sdcard");
        if (theSDCardObject->isMounted()) {
            ESP_LOGI(TAG, "SD card mounted ");
            const char* ELOC_FOLDER = "/sdcard/eloc";
            if (!ffsutil::folderExists(ELOC_FOLDER)) {
                ESP_LOGI(TAG, "%s does not exist, creating empty folder", ELOC_FOLDER);
                mkdir(ELOC_FOLDER, 0777);
            }
            float freeSpace = theSDCardObject->freeSpaceGB();
            float totalSpace = theSDCardObject->getCapacityMB()/1024;
            ESP_LOGI(TAG, "SD card %f / %f GB free", freeSpace, totalSpace);
        }
    #endif
    gMountedSDCard = theSDCardObject->isMounted();
    return gMountedSDCard;
}

esp_err_t checkSDCard() {

    if (!gMountedSDCard) {
        // in case SD card is not yet mounted, mout it now, e.g. not inserted durint boot
        if (!mountSDCard()) {
            return ESP_ERR_NOT_FOUND;
        }
    }
    // getupdated free space
    float freeSpaceGB = theSDCardObject->freeSpaceGB();
    if ((freeSpaceGB > 0.0) && (freeSpaceGB < 0.5)) {
        //  btwrite("!!!!!!!!!!!!!!!!!!!!!");
        //  btwrite("SD Card full. Cannot record");
        //  btwrite("!!!!!!!!!!!!!!!!!!!!!");
        ESP_LOGE(TAG, "SD card is full, Free space %.3f GB", freeSpaceGB);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}


void saveStatusToSD() {
    String sendstring;

    sendstring = sendstring + "Session ID:  " + gSessionIdentifier + "\n";

    sendstring = sendstring + "Session Start Time:  " + String(timeObject.getYear()) + "-" + String(timeObject.getMonth()) + "-" +
                String(timeObject.getDay()) + " " + String(timeObject.getHour(true)) + ":" + String(timeObject.getMinute()) + ":" +
                String(timeObject.getSecond()) + "\n";

    sendstring = sendstring + "Firmware Version:  " + gFirmwareVersion + "\n"; // firmware

    sendstring = sendstring + "File Header:  " + getDeviceInfo().fileHeader + "\n"; // file header

    sendstring = sendstring + "Bluetooh on when Record?:   " + (getConfig().bluetoothEnableDuringRecord ? "on" : "off") + "\n";

    sendstring = sendstring + "Sample Rate:  "      + String(getMicInfo().MicSampleRate) + "\n";
    sendstring = sendstring + "Seconds Per File:  " + String(getConfig().secondsPerFile) + "\n";

    // sendstring=sendstring+   "Voltage Offset:  " +String(gVoltageOffset)                  + "\n" ;
    sendstring = sendstring + "Mic Type:  "         + getMicInfo().MicType + "\n";
    sendstring = sendstring + "SD Card Free GB:   " + String(theSDCardObject->freeSpaceGB()) + "\n";
    sendstring = sendstring + "Mic Gain:  "         + String(getMicInfo().MicBitShift) + "\n";
    sendstring = sendstring + "GPS Location:  "     + getDeviceInfo().locationCode + "\n";
    sendstring = sendstring + "GPS Accuracy:  "     + getDeviceInfo().locationAccuracy + " m\n";

    // sendstring=sendstring+ "\n\n";

    FILE *fp;
    String temp = "/sdcard/eloc/" + gSessionIdentifier + "/" + "config_" + gSessionIdentifier + ".txt";
    fp = fopen(temp.c_str(), "wb");
    // fwrite()
    // String temp=
    // String temp="/sdcard/eloc/test.txt";
    // File file = SD.open(temp.c_str(), FILE_WRITE);
    // file.print(sendstring);
    fputs(sendstring.c_str(), fp);
    fclose(fp);
}


i2s_config_t getI2sConfig() {
    // update the config with the updated parameters
    ESP_LOGI(TAG, "Sample rate = %d", getMicInfo().MicSampleRate);
    i2s_mic_Config.sample_rate = getMicInfo().MicSampleRate; //fails when hardcoded to 22050
    i2s_mic_Config.use_apll = getMicInfo().MicUseAPLL; //not getting set. getConfig().MicUseAPLL, //the only thing that works with LowPower/APLL is 16khz 12khz??
    if (i2s_mic_Config.sample_rate == 0) {
        ESP_LOGI(TAG, "Resetting invalid sample rate to default = %d", I2S_DEFAULT_SAMPLE_RATE);
        i2s_mic_Config.sample_rate = I2S_DEFAULT_SAMPLE_RATE;
    }
    return i2s_mic_Config;
}

void app_main(void) {

    ESP_LOGI(TAG, "\nSETUP--start\n");
    initArduino();
    ESP_LOGI(TAG, "initArduino done");

    printPartitionInfo(); // so if reboots, always boot into the bluetooth partition

    ESP_LOGI(TAG, "\n"
        "------------------------- ELOC Recorder -------------------------\n"
        "-- VERSION: %s\n"
        "-----------------------------------------------------------------\n", VERSIONTAG);

    initTime();

    printRevision();

    resetPeripherals();

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

    // new
    gpio_set_direction(GPIO_BUTTON, GPIO_MODE_INPUT); //
    gpio_set_pull_mode(GPIO_BUTTON, GPIO_PULLUP_ONLY);
    // end new
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

    gpio_sleep_sel_dis(I2S_MIC_SERIAL_CLOCK);
    gpio_sleep_sel_dis(I2S_MIC_LEFT_RIGHT_CLOCK);
    gpio_sleep_sel_dis(I2S_MIC_SERIAL_DATA);

    
    gpio_sleep_sel_dis(PIN_NUM_MISO);
    gpio_sleep_sel_dis(PIN_NUM_CLK);
    gpio_sleep_sel_dis(PIN_NUM_MOSI);
    gpio_sleep_sel_dis(PIN_NUM_CS);


    gpio_set_direction(GPIO_BUTTON, GPIO_MODE_INPUT); //
    gpio_set_pull_mode(GPIO_BUTTON, GPIO_PULLUP_ONLY);
    gpio_set_intr_type(GPIO_BUTTON, GPIO_INTR_POSEDGE);
    gpio_sleep_sel_dis(GPIO_BUTTON);

#endif

    gpio_set_level(STATUS_LED, 0);
    gpio_set_level(BATTERY_LED, 0);

    ESP_LOGI(TAG, "Setting up HW System...");
    ElocSystem::GetInstance();

    ESP_LOGI(TAG, "Setting up Battery...");
    Battery::GetInstance();

    if (!SPIFFS.begin(true, "/spiffs")) {
        ESP_LOGI(TAG, "An Error has occurred while mounting SPIFFS");
        // return;
    }
    mountSDCard();

    // print some file system info
     ESP_LOGI(TAG, "File system loaded: ");
    ffsutil::printListDir("/spiffs");
    ffsutil::printListDir("/sdcard");
    ffsutil::printListDir("/sdcard/eloc");
    
    readConfig();
    //setup persistent logging only if SD card is mounted
    if (theSDCardObject && theSDCardObject->isMounted()) {
        const logConfig_t& cfg= getConfig().logConfig;
        esp_err_t err = Logging::init(cfg.logToSdCard, cfg.filename, cfg.maxFiles, cfg.maxFileSize);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initilize logging subsystem with %s", esp_err_to_name(err));
        }
    }

    // check if a firmware update is triggered via SD card
    checkForFirmwareUpdateFile();


    rec_req_evt_queue = xQueueCreate(10, sizeof(rec_req_t));
    xQueueReset(rec_req_evt_queue);
    ESP_ERROR_CHECK(gpio_install_isr_service(GPIO_INTR_PRIO));

    ESP_LOGI(TAG, "Creating Bluetooth  task...");
    if (esp_err_t err = BluetoothServerSetup(false)) {
        ESP_LOGI(TAG, "BluetoothServerSetup failed with %s", esp_err_to_name(err));
    }

    #ifdef USE_PERF_MONITOR
        ESP_LOGI(TAG, "Creating Performance Monitor task...");
        if (esp_err_t err = PerfMonitor::setup()) {
            ESP_LOGI(TAG, "Performance Monitor failed with %s", esp_err_to_name(err));
        }
    #endif

    // setup button as interrupt
    ESP_ERROR_CHECK(gpio_isr_handler_add(GPIO_BUTTON, buttonISR, (void *)GPIO_BUTTON));
    //ESP_ERROR_CHECK(gpio_isr_handler_add(GPIO_BUTTON, buttonISR, (void *)OTHER_GPIO_BUTTON));

    /** Setup Power Management */
    esp_pm_config_esp32_t cfg = {
        .max_freq_mhz = getConfig().cpuMaxFrequencyMHZ, 
        .min_freq_mhz = getConfig().cpuMinFrequencyMHZ, 
        .light_sleep_enable = getConfig().cpuEnableLightSleep
    };
    esp_pm_configure(&cfg);
    // calling gpio_sleep_sel_dis must be done after esp_pm_configure() otherwise they will get overwritten
    // interrupt must be enabled during sleep otherwise they might continously trigger (missing pullup)
    // or won't work at all
    gpio_sleep_sel_dis(GPIO_BUTTON);
    gpio_sleep_sel_dis(LIS3DH_INT_PIN);
    // now setup GPIOs as wakeup source. This is required to wake up the recorder in tickless idle
    //BUGME: this seems to be a bug in ESP IDF: https://github.com/espressif/arduino-esp32/issues/7158
    //       gpio_wakeup_enable does not correctly switch to rtc_gpio_wakeup_enable() for RTC GPIOs.
    ESP_ERROR_CHECK(rtc_gpio_wakeup_enable(LIS3DH_INT_PIN, GPIO_INTR_HIGH_LEVEL));
    ESP_ERROR_CHECK(rtc_gpio_wakeup_enable(GPIO_BUTTON, GPIO_INTR_LOW_LEVEL));
    ESP_ERROR_CHECK(esp_sleep_enable_gpio_wakeup());

    if (getConfig().testI2SClockInput)
        testInput();

    ESP_LOGI(TAG, "waiting for button or bluetooth");
    ESP_LOGI(TAG, "Battery: Voltage: %.3fV, %.0f%% SoC", Battery::GetInstance().getVoltage(), Battery::GetInstance().getSoC());
    while (true) {

        rec_req_t rec_req = REC_REQ_NONE;
        if (xQueueReceive(rec_req_evt_queue, &rec_req, pdMS_TO_TICKS(500))) {
            ESP_LOGI(TAG,"REC_REQ = %d", rec_req);
            if (rec_req == REC_REQ_START) {
                if (esp_err_t err = checkSDCard() != ESP_OK) {
                    ESP_LOGE(TAG, "Cannot start recording due to SD error %s", esp_err_to_name(err));
                    // TODO: set status here and let LED flash as long as the device is in error state
                    LEDflashError();
                } else {
                    getI2sConfig(); 
                    I2SMEMSSampler input (I2S_NUM_0, i2s_mic_pins, i2s_mic_Config, getMicInfo().MicBitShift,getConfig().listenOnly, getMicInfo().MicUseTimingFix);
                    record(&input);
                }
            }
        }
    }
}
