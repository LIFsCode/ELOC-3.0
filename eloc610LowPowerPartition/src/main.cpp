
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

#include "lis3dh.h"
#include "Battery.hpp"
#include "ElocSystem.hpp"
#include "ElocConfig.hpp"
#include "BluetoothServer.hpp"

static const char *TAG = "main";


bool gMountedSDCard=false;
bool gRecording=false;


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

//timing
int64_t gTotalUPTimeSinceReboot=esp_timer_get_time();  //esp_timer_get_time returns 64-bit time since startup, in microseconds.
int64_t gTotalRecordTimeSinceReboot=0;
int64_t gSessionRecordTime=0;

int gMinutesWaitUntilDeepSleep=60; //change to 1 or 2 for testing


//session stuff
String gSessionIdentifier="";

//String gBluetoothMAC="";
String gFirmwareVersion=VERSION;

ESP32Time timeObject;
//WebServer server(80);
//bool updateFinished=false;
bool gWillUpdate=false;
float gFreeSpaceGB=0.0;
uint32_t  gFreeSpaceKB=0;

//String gTimeDifferenceCode; //see getTimeDifferenceCode() below

 #ifdef USE_SPI_VERSION
    SDCard *theSDCardObject;
 #endif

#ifdef USE_SDIO_VERSION
    SDCardSDIO *theSDCardObject;
 #endif


void mountSDCard();
void sendElocStatus();
void writeSettings(String settings);
void freeSpace();
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
  rec_req_t rec_req = gRecording ? REC_REQ_STOP : REC_REQ_START;
  //ets_printf("button pressed");
  xQueueSendFromISR(rec_req_evt_queue, &rec_req, NULL);
     
     //detachInterrupt(GPIO_BUTTON);

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


void changeBootPartition() {  ///will always set boot partition back to partition0 except if it 
    
    
    const esp_partition_t *partition = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, "partition0");
    //ESP_LOGI(TAG, "%d  address", partition->address);
    
    if (partition==NULL) {
           ESP_LOGE(TAG, "\npartition0 not found. Are you sure you built/flashed it correctly? \n");
           delay(2000);
    } else {
      esp_ota_set_boot_partition(partition);
       ESP_LOGI(TAG, "changed boot partition to  partition0");
      //vTaskDelay(pdMS_TO_TICKS(5000));
      //esp_restart();

    }

}

long getTimeFromTimeObjectMS() {
    return(timeObject.getEpoch()*1000L+timeObject.getMillis());

}










void time() {

        struct timeval tv_now;
      gettimeofday(&tv_now, NULL);
      int64_t time_us = (     (int64_t)tv_now.tv_sec      * 1000000L) + (int64_t)tv_now.tv_usec;
      time_us=time_us/1000;
   
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


void createFilename( char *fname) {
   fname[0]='\0';
   strcat(fname,"/sdcard/eloc/");
    strcat(fname,gSessionFolder);
     strcat(fname,"/");
     strcat(fname,gSessionFolder);
    strcat(fname,"_");
 

    strcat(fname, std::to_string(getSystemTimeMS()).c_str());
     strcat(fname,".wav");
      ESP_LOGI(TAG, "filename %s", fname);

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

esp_err_t checkSDCard() {

    if (!gMountedSDCard) {
        return ESP_ERR_NOT_FOUND;
    }
    // getupdated free space
    freeSpace();
    if ((gFreeSpaceGB > 0.0) && (gFreeSpaceGB < 0.5)) {
        //  btwrite("!!!!!!!!!!!!!!!!!!!!!");
        //  btwrite("SD Card full. Cannot record");
        //  btwrite("!!!!!!!!!!!!!!!!!!!!!");
        ESP_LOGE(TAG, "SD card is full, Free space %.3f GB", gFreeSpaceGB);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void record(I2SSampler *input) {
  
  gRecording=true;
  char fname[100];
  
    input->start();
  gRealSampleRate=(int32_t)(i2s_get_clk(I2S_NUM_0));
  ESP_LOGI(TAG, "I2s REAL clockrate in record  %u", gRealSampleRate  );

  
   bool deepSleep=false;
  float loops;
  int samples_read;
  int64_t longestWriteTimeMillis=0;
  int64_t writeTimeMillis=0;
  int64_t bufferUnderruns=0;
  float bufferTimeMillis=(((float)gBufferCount*(float)gBufferLen)/(float)getMicInfo().MicSampleRate)*1000;
  int64_t writestart,writeend,loopstart,looptime,temptime;
  

  
 
  

  graw_samples = (int32_t *)malloc(sizeof(int32_t) * 1000);
  int16_t *samples = (int16_t *)malloc(sizeof(int16_t) *gSampleBlockSize);
 
 
  
  FILE *fp=NULL;
  WAVFileWriter *writer=NULL;

 
 
  bool stopit = false;
  

  
  if (getConfig().listenOnly)  ESP_LOGI(TAG, "Listening only...");
  while (!stopit) {
      
      
      if (!getConfig().listenOnly) {
            createFilename(fname);
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
  


 

  ESP_LOGI(TAG, "Finished recording");
  //btwrite("Finished recording");
   gRecording=false;
 
   if (deepSleep) doDeepSleep();

  

}




void mountSDCard() {
    gMountedSDCard=false;
    #ifdef USE_SPI_VERSION
          ESP_LOGI(TAG, "TRYING to mount SDCArd, SPI ");
          theSDCardObject = new SDCard("/sdcard",PIN_NUM_MISO, PIN_NUM_MOSI, PIN_NUM_CLK, PIN_NUM_CS);
    #endif 
    
    #ifdef USE_SDIO_VERSION

          ESP_LOGI(TAG, "TRYING to mount SDCArd, SDIO ");
           theSDCardObject = new SDCardSDIO("/sdcard");
           if (gMountedSDCard) {
            ESP_LOGI(TAG, "SD card mounted ");
           }
    #endif
}



void freeSpace() {

    // FATFS *fs;
    // uint32_t fre_clust, fre_sect, tot_sect;
    // FRESULT res;
    // /* Get volume information and free clusters of drive 0 */
    // res = f_getfree("0:", &fre_clust, &fs);
    // /* Get total sectors and free sectors */
    // tot_sect = (fs->n_fatent - 2) * fs->csize;
    // fre_sect = fre_clust * fs->csize;

    // /* Print the free space (assuming 512 bytes/sector) */
    // printf("%10lu KiB total drive space.\n%10lu KiB available.\n",
    //        tot_sect / 2, fre_sect / 2);


    //   gFreeSpaceGB= float((float)fre_sect/1048576.0 /2.0);
    //   printf("\n %2.1f GB free\n", gFreeSpaceGB);

    //   gFreeSpaceKB=fre_sect / 2;
    //   printf("\n %u KB free\n", gFreeSpaceKB);

}



bool folderExists(const char* folder)
{
    //folder = "/sdcard/eloc";
    struct stat sb;

    if (stat(folder, &sb) == 0 && S_ISDIR(sb.st_mode)) {
        ESP_LOGI(TAG, "yes");
         return true;
    } else {
         ESP_LOGI(TAG, "no");
         return false;
    }
  return false;
}


void saveStatusToSD() {
       String sendstring;
       
      sendstring=sendstring+   "Session ID:  " +gSessionIdentifier+   "\n" ;
      
     sendstring=sendstring+   "Session Start Time:  "    +String(timeObject.getYear())+"-"  +String(timeObject.getMonth())+"-" +String(timeObject.getDay())+" " +String(timeObject.getHour(true))+":" +String(timeObject.getMinute())+":"  +String(timeObject.getSecond())           + "\n" ;

          
      sendstring=sendstring+   "Firmware Version:  "+          gFirmwareVersion                    + "\n" ; //firmware
      
    
      sendstring=sendstring+   "File Header:  "+     getDeviceInfo().location                         + "\n" ; //file header
  
      sendstring=sendstring+   "Bluetooh on when Record?:   " + (getConfig().bluetoothEnableDuringRecord ? "on" : "off")             + "\n" ;
  
      sendstring=sendstring+   "Sample Rate:  " +String(getMicInfo().MicSampleRate)               + "\n" ;
      sendstring=sendstring+   "Seconds Per File:  " +String(getConfig().secondsPerFile)               + "\n" ;
 
      
  
       //sendstring=sendstring+   "Voltage Offset:  " +String(gVoltageOffset)                  + "\n" ;
       sendstring=sendstring+   "Mic Type:  " +getMicInfo().MicType                  + "\n" ;
        sendstring=sendstring+   "SD Card Free GB:   "+ String(gFreeSpaceGB)                  + "\n" ;
       sendstring=sendstring+   "Mic Gain:  " +String(getMicInfo().MicBitShift)                  + "\n" ;
       sendstring=sendstring+   "GPS Location:  " +getDeviceInfo().locationCode                + "\n" ;
      sendstring=sendstring+    "GPS Accuracy:  " +getDeviceInfo().locationAccuracy                + " m\n" ;
     
       // sendstring=sendstring+ "\n\n";
     
      FILE *fp;
      String temp= "/sdcard/eloc/"+gSessionIdentifier+"/"+"config_"+gSessionIdentifier+".txt";
      fp = fopen(temp.c_str(), "wb");
      //fwrite()
      //String temp=
      //String temp="/sdcard/eloc/test.txt";
      //File file = SD.open(temp.c_str(), FILE_WRITE);
      //file.print(sendstring);
      fputs(sendstring.c_str(), fp);
      fclose(fp);
     
      File file = SPIFFS.open("/currentsession.txt", FILE_WRITE); 
      file.print(sendstring);
      file.close();


  

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
        // ESP_LOGI(TAG, "i2s_mic_Config = "
        //             "\n\tmode = %d"
        //             "\n\tsample_rate = %d"
        //             "\n\tbits_per_sample = %d"
        //             "\n\tbits_per_chan = %d"
        //             "\n\tchannel_format = %d"
        //             "\n\tcommunication_format = %d"
        //             "\n\tintr_alloc_flags = %d"
        //             "\n\tdma_buf_count = %d"
        //             "\n\tdma_buf_len = %d"
        //             "\n\tuse_apll = %s"
        //             "\n\ttx_desc_auto_clear = %s"
        //             "\n\tfixed_mclk = %d",
        //             i2s_mic_Config.mode,
        //             i2s_mic_Config.sample_rate,
        //             i2s_mic_Config.bits_per_sample,
        //             i2s_mic_Config.bits_per_chan,
        //             i2s_mic_Config.channel_format,
        //             i2s_mic_Config.communication_format,
        //             i2s_mic_Config.intr_alloc_flags,
        //             i2s_mic_Config.dma_buf_count,
        //             i2s_mic_Config.dma_buf_len,
        //             i2s_mic_Config.use_apll ? "True":"false",
        //             i2s_mic_Config.tx_desc_auto_clear ? "True":"false",
        //             i2s_mic_Config.fixed_mclk
        //             );
    return i2s_mic_Config;
}

void app_main(void) {

    ESP_LOGI(TAG, "\n\n---------VERSION %s\n\n", VERSIONTAG);
    initArduino();
    ESP_LOGI(TAG, "initArduino done");

    ESP_LOGI(TAG, "\nSETUP--start\n");

    changeBootPartition(); // so if reboots, always boot into the bluetooth partition

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

    // delay(30000);

    printMemory();

    if (!SPIFFS.begin(true, "/spiffs")) {
        ESP_LOGI(TAG, "An Error has occurred while mounting SPIFFS");
        // return;
    }
    mountSDCard();
    freeSpace();

    //BUGME: do not read settings from SPIFFS because there is currently no useable default handling
    //       in case the file is corrupted or does not exist
    // readSettings();
    readMicInfo();

    rec_req_evt_queue = xQueueCreate(10, sizeof(rec_req_t));
    xQueueReset(rec_req_evt_queue);
    ESP_ERROR_CHECK(gpio_install_isr_service(GPIO_INTR_PRIO));

    ESP_LOGI(TAG, "Creating Bluetooth  task...");
    if (esp_err_t err = BluetoothServerSetup(false)) {
        ESP_LOGI(TAG, "BluetoothServerSetup failed with %s", esp_err_to_name(err));
    }

    // setup button as interrupt
    ESP_ERROR_CHECK(gpio_isr_handler_add(GPIO_BUTTON, buttonISR, (void *)GPIO_BUTTON));
    //ESP_ERROR_CHECK(gpio_isr_handler_add(GPIO_BUTTON, buttonISR, (void *)OTHER_GPIO_BUTTON));

    readConfig();

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
    ESP_LOGI(TAG, "voltage is %.3f", Battery::GetInstance().getVoltage());
    while (true) {

        rec_req_t rec_req = REC_REQ_NONE;
        // wait_for_button_push();
        // record(input);
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
