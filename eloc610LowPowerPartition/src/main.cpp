
#include "esp_err.h"
#include "esp_log.h"
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
#include "ManualWakeup.hpp"

static const char *TAG = "main";


bool gMountedSDCard=false;
bool gRecording=false;


int32_t *graw_samples;

int gRawSampleBlockSize=1000;
int gSampleBlockSize=16000; //must be factor of 1000   in samples
int gBufferLen=1000; //in samples
int gBufferCount=18;   // so 6*4000 = 24k buf len
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

bool gSentSettings=false;
//String gBluetoothMAC="";
String gFirmwareVersion=VERSION;

BluetoothSerial SerialBT;
ESP32Time timeObject;
//WebServer server(80);
//bool updateFinished=false;
bool gWillUpdate=false;
bool gGPIOButtonPressed=false;
float gFreeSpaceGB=0.0;
uint32_t  gFreeSpaceKB=0;
long gLastSystemTimeUpdate; // local system time of last time update PLUS minutes since last phone update 
String gSyncPhoneOrGoogle; //will be either G or P (google or phone).
String gLocationCode="unknown";
String gLocationAccuracy="99";
//String gTimeDifferenceCode; //see getTimeDifferenceCode() below
bool gBlueToothISDisabled=false;

 #ifdef USE_SPI_VERSION
    SDCard *theSDCardObject;
 #endif

#ifdef USE_SDIO_VERSION
    SDCardSDIO *theSDCardObject;
 #endif


void mountSDCard();
void btwrite(String theString);
void sendElocStatus();
void sendSettings();
void readSettings();
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
                 i2s_mic_Config.use_apll=gUseAPLL;
           

                 input = new I2SMEMSSampler(I2S_NUM_0, i2s_mic_pins, i2s_mic_Config,gTimingFix); //the true at the end is the timing fix
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


static  void IRAM_ATTR buttonISR(void *args) {
  //return;
  //ESP_LOGI(TAG, "button pressed");
  //delay(5000);
   if (gRecording)  {
    
        gGPIOButtonPressed=true;
     
    } else {
      gGPIOButtonPressed=false;

    }
     
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


void wait_for_button_push()
{
 
  //digitalWrite(BATTERY_LED,LOW);
  //bool gBatteryLEDToggle=false;
  float currentvolts;
  currentvolts= Battery::GetInstance().getVoltage();
  
  gRecording=false;
  //Battery::GetInstance().getVoltage();
  boolean sentElocStatus=false;
  int loopcounter=0;
  ESP_LOGI(TAG,  "waiting for button or bluetooth");
  ESP_LOGI(TAG,  "voltage is %.3f", Battery::GetInstance().getVoltage());
  
  
  int64_t timein= getSystemTimeMS();


  int gotrecord=false;
  int leddelay;
  String serialIN;
 
  ESP_LOGI(TAG, " two following are heap size, total and max alloc ");
  ESP_LOGI(TAG, "     %u", ESP.getFreeHeap());
  ESP_LOGI(TAG, "     %u", ESP.getMaxAllocHeap()); 
  //mountSDCard();
  //freeSpace();
 
  while (!gotrecord)
  {
      //Battery::GetInstance().getVoltage();
      //gotrecord=false;
      //ESP_LOGI(TAG,  "waiting for buttonpress");
      //btwrite("Waiting for record button");    
      if (SerialBT.connected()) {
        if (!gSentSettings) {



          //vTaskDelay(pdMS_TO_TICKS(200));
          mountSDCard();
          vTaskDelay(pdMS_TO_TICKS(200));
          sendSettings();
          vTaskDelay(pdMS_TO_TICKS(200));
          freeSpace();
          vTaskDelay(pdMS_TO_TICKS(200));
          btwrite("getClk\n");
          //vTaskDelay(pdMS_TO_TICKS(50));
          //vTaskDelay(pdMS_TO_TICKS(800));
          //sendElocStatus();
          //if (gFreeSpaceGB!=0.0) btwrite("SD card free: "+String(gFreeSpaceGB)+" GB");
          //vTaskDelay(pdMS_TO_TICKS(100));

          //btwrite(SD);
          gSentSettings=true;
          //vTaskDelay(pdMS_TO_TICKS(200));

          //btwrite("#"+String(gSampleRate)+"#"+String(gSecondsPerFile)+"#"+gLocation); 
        } 
      } else {
          gSentSettings=false; 
          sentElocStatus=false;
      }
      //gotCommand=false;
      if (SerialBT.available()) {
        // handle case for sending initial default setup to app
        serialIN=SerialBT.readString();
        //ESP_LOGI(TAG, serialIN);
          //if (serialIN.startsWith("settingsRequest")) {
          //   btwrite("#"+String(gSampleRate)+"#"+String(gSecondsPerFile)+"#"+gLocation); 
          //}
          
          
           if (serialIN.startsWith("record")) {
              btwrite("\n\nYou are using an old version of the Android app. Please upgrade\n\n");

           }
          
          if (serialIN.startsWith( "_setClk_")) {
                   ESP_LOGI(TAG, "setClk starting");
                  //string will look like _setClk_G__120____32456732728  //if google, 12 mins since last phone sync 
                  //                   or _setClk_P__0____43267832648  //if phone, 0 min since last phone sync
                  
                  
                  String everything = serialIN.substring(serialIN.indexOf("___")+3,serialIN.length());
                  everything.trim();
                  String seconds = everything.substring(0,10); //was 18
                  String milliseconds=everything.substring(10,everything.length()); 
                  String tmp = "timestamp in from android GMT "+everything    +"  sec: "+seconds + "   millisec: "+milliseconds;
                  ESP_LOGI(TAG, "%s", tmp.c_str());
                  String minutesSinceSync=serialIN.substring(11,serialIN.indexOf("___"));
                  gSyncPhoneOrGoogle=serialIN.substring(8,9);
                  //ESP_LOGI(TAG, minutesSinceSync);
                // ESP_LOGI(TAG, "GorP: "+GorP);
                //delay(8000);
                // ESP_LOGI(TAG, test);
                  //ESP_LOGI(TAG, seconds);
                  //ESP_LOGI(TAG, milliseconds);
                  milliseconds.trim();
                  if (milliseconds.length()<2) milliseconds="0";
                  timeObject.setTime(atol(seconds.c_str())+(TIMEZONE_OFFSET*60L*60L),  (atol(milliseconds.c_str()))*1000    );
                  //timeObject.setTime(atol(seconds.c_str()),  (atol(milliseconds.c_str()))*1000    );
                  // timestamps coming in from android are always GMT (minus 7 hrs)
                  // if I not add timezone then timeobject is off 
                  // so timeobject does not seem to be adding timezone to system time.
                  // timestamps are in gmt+0, so timestamp convrters
                
                  struct timeval tv_now;
                  gettimeofday(&tv_now, NULL);
                  int64_t time_us = (     (int64_t)tv_now.tv_sec      * 1000000L) + (int64_t)tv_now.tv_usec;
                  time_us=time_us/1000;
                  
                  //ESP_LOGI(TAG, "atol(minutesSinceSync.c_str()) *60L*1000L "+String(atol(minutesSinceSync.c_str()) *60L*1000L));
                  gLastSystemTimeUpdate=getTimeFromTimeObjectMS() -(      atol(minutesSinceSync.c_str()) *60L*1000L);
                  timein= getSystemTimeMS();
                  //ESP_LOGI(TAG, "timestamp in from android GMT "+everything    +"  sec: "+seconds + "   millisec: "+milliseconds);
                  //ESP_LOGI("d", "new timestamp from new sys time (local time) %lld", time_us  ); //this is 7 hours too slow!
                  //ESP_LOGI("d","new timestamp from timeobJect (local time) %lld",gLastSystemTimeUpdate);
                  

                  
                  //btwrite("time: "+timeObject.getDateTime()+"\n");
                   if (!sentElocStatus) {
                      sentElocStatus=true;
                      sendElocStatus();
                   }
                   ESP_LOGI(TAG, "setClk ending");
          }          
    
    
          if (serialIN.startsWith( "setGPS")) {
               // read the location on startup? 
               //only report recorded location status? 
               // need to differentiate between manual set and record set.
              gLocationCode=  serialIN.substring(serialIN.indexOf("^")+1,serialIN.indexOf("#") );
              gLocationCode.trim();
              gLocationAccuracy= serialIN.substring(serialIN.indexOf("#")+1,serialIN.length() );
              gLocationAccuracy.trim();
              ESP_LOGI(TAG, "loc: %s   acc %s", gLocationCode.c_str(), gLocationAccuracy.c_str());
              
              File file = SPIFFS.open("/gps.txt", FILE_WRITE); 
              file.println(gLocationCode);
              file.println(gLocationAccuracy);
               gotrecord=true;
 
              //btwrite("GPS Location set");

          }         

          if (serialIN.startsWith("_record_")) {
 
            gotrecord=true;
            } else {
                //const char *converted=serialIN.c_str();
               /* if (serialIN.startsWith( "8k")){gSampleRate=8000;btwrite("sample rate changed to 8k");gotCommand=true;}
                if (serialIN.startsWith( "16k")){gSampleRate=16000;btwrite("sample rate changed to 16k");gotCommand=true;}
                if (serialIN.startsWith( "22k")){gSampleRate=22000;btwrite("sample rate changed to 22k");gotCommand=true;}
                if (serialIN.startsWith( "32k")){gSampleRate=32000;btwrite("sample rate changed to 32k");gotCommand=true;}
                if (serialIN.startsWith( "10s")){gSecondsPerFile=10;btwrite("10 secs per file");gotCommand=true;}
                if (serialIN.startsWith( "1m")){gSecondsPerFile=60;btwrite("1 minute per file");gotCommand=true;}
                if (serialIN.startsWith( "5m")){gSecondsPerFile=300;btwrite("5 minutes per file");gotCommand=true;}
                if (serialIN.startsWith( "1h")){gSecondsPerFile=3600;btwrite("1 hour per file");gotCommand=true;}
                if (serialIN.startsWith( "settingsRequest")){gotCommand=true;}
                if (!gotCommand) btwrite("command not found. options are 8k 16k 22k 32k  and 10s 1m 5m 1h");
                */

                    
               
                

              
                if (serialIN.startsWith( "#settings")) {
                    writeSettings(serialIN);
                    vTaskDelay(pdMS_TO_TICKS(200));
                    readSettings();
                    vTaskDelay(pdMS_TO_TICKS(200));
                    btwrite("settings updated");
                     vTaskDelay(pdMS_TO_TICKS(500));
                     sendElocStatus();

                }

 


 

              
            }



      }
    
      
      if (gotrecord) {
        mountSDCard();
        if (!gMountedSDCard) { 
          //mountSDCard();
          LEDflashError();
          gotrecord=false;
          sendSettings();
        }

          if ((gFreeSpaceGB > 0.0)&& (gFreeSpaceGB < 0.5) ) {
            btwrite("!!!!!!!!!!!!!!!!!!!!!");
            btwrite("SD Card full. Cannot record");
            btwrite("!!!!!!!!!!!!!!!!!!!!!");
            LEDflashError();
            gotrecord=false;
            sendSettings();


          }      
      
      
      }
      
      loopcounter++;
      if (loopcounter==30) loopcounter=0;
      vTaskDelay(pdMS_TO_TICKS(30)); //so if we get record, max 10ms off
     

      /**************** NOTE: Status LED blinking must be handled in a separate task 
         if (loopcounter==0) {
                currentvolts= Battery::GetInstance().getVoltage();
                //currentvolts=0.1;
                if ((getSystemTimeMS()-timein) > (60000*gMinutesWaitUntilDeepSleep)) doDeepSleep(); // one hour to deep sleep 
                if (currentvolts <= gvOff) doDeepSleep();
                 digitalWrite(STATUS_LED,HIGH);
                
                
                digitalWrite(BATTERY_LED,LOW);
                if (currentvolts <= gvLow) digitalWrite(BATTERY_LED,HIGH);
                if (currentvolts >= gvFull) digitalWrite(BATTERY_LED,HIGH);
                
           } else {
                if (!SerialBT.connected()) digitalWrite(STATUS_LED,LOW);
                if (currentvolts <= gvLow) digitalWrite(BATTERY_LED,LOW);
           }
        ****************************************************************************/
  }
 
 //mountSDCard();
 gSentSettings=false; 

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
   
       esp_sleep_enable_ext0_wakeup(OTHER_GPIO_BUTTON, 0); //then try changing between 0 and 1.

      esp_sleep_enable_ext0_wakeup(GPIO_BUTTON, 0); //try commenting this out
     
      
     // printf("Going to sleep now");
      delay(2000);
      esp_deep_sleep_start(); //change to deep?  
      
      //printf("OK button was pressed.waking up");
      delay(2000);
      esp_restart();


}











void record(I2SSampler *input) {
  
  gRecording=true;
  char fname[100];
  
  //BUGME: this is very dirty: I2SMEMSSampler directly accesses global variable gbitShift
  //        the high power partition uses gMicBitShift (WTF?) but its never set through config
  gbitShift=getMicInfo().MicBitShift.toInt();

    input->start();
  gRealSampleRate=(int32_t)(i2s_get_clk(I2S_NUM_0));
  ESP_LOGI(TAG, "I2s REAL clockrate in record  %u", gRealSampleRate  );

  
   bool deepSleep=false;
  float loops;
  int samples_read;
  int64_t longestWriteTimeMillis=0;
  int64_t writeTimeMillis=0;
  int64_t bufferUnderruns=0;
  float bufferTimeMillis=(((float)gBufferCount*(float)gBufferLen)/(float)gSampleRate)*1000;
  int64_t writestart,writeend,loopstart,looptime,temptime;
  

  
 
  

  graw_samples = (int32_t *)malloc(sizeof(int32_t) * 1000);
  int16_t *samples = (int16_t *)malloc(sizeof(int16_t) *gSampleBlockSize);
 
 
  
  FILE *fp=NULL;
  WAVFileWriter *writer=NULL;

 
 
   static char sBuffer[ 240 ]; // 40 B per task
  
  gGPIOButtonPressed=false;
  bool stopit = false;
  

  
  if (gListenOnly)  ESP_LOGI(TAG, "Listening only...");
  while (!stopit) {
      
      
      if (!gListenOnly) {
            createFilename(fname);
            fp = fopen(fname, "wb");

          
            writer = new WAVFileWriter(fp, gRealSampleRate, NUMBER_OF_CHANNELS);
      }
      long loopCounter=0;
      int64_t recordupdate = esp_timer_get_time();
 
 
 
      loops= (float)gSecondsPerFile/((gSampleBlockSize)/((float)gSampleRate)) ;
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
            
             if (!gListenOnly)  {
                  writestart = esp_timer_get_time();
                  writer->write(samples, samples_read);
                  
                  writeend = esp_timer_get_time();
                  writeTimeMillis=(writeend - writestart)/1000;
                  if (writeTimeMillis > longestWriteTimeMillis) longestWriteTimeMillis=writeTimeMillis;
                  if (writeTimeMillis>bufferTimeMillis) bufferUnderruns++;
                  ESP_LOGI(TAG, "Wrote %d samples in %lld ms. Longest: %lld. buffer (ms): %f underrun: %lld loop:%lld",   samples_read,writeTimeMillis,longestWriteTimeMillis,bufferTimeMillis,bufferUnderruns,looptime/1000);
            }
            //if (gListenOnly)  ESP_LOGI(TAG, "Listening only...");
            if (gGPIOButtonPressed)  {stopit=true; loopCounter=10000001L; ESP_LOGI(TAG,"gpio button pressed");}

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
    
    if (!gListenOnly) {
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



void btwrite(String theString){
 //FILE *fp;
  if (gBlueToothISDisabled) return;

  //SerialBT.
  if (SerialBT.connected()) {
    SerialBT.println(theString);
  }
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


String readNodeName() {

      // int a =gDeleteMe.length();
      // if (a==2) {a=1;}
      if(!(SPIFFS.exists("/nodename.txt"))){
  
        ESP_LOGI(TAG, "No nodename set. Returning ELOC_NONAME");
        return("ELOC_NONAME");
          

      }
 

  File file2 = SPIFFS.open("/nodename.txt", FILE_READ);
  
  //String temp = file2.readStringUntil('\n');

  String temp = file2.readString();
  temp.trim();
  file2.close();
  ESP_LOGI(TAG, "node name: %s", temp.c_str());
  return(temp);
 //return("");

}





void saveStatusToSD() {
       String sendstring;
       
      sendstring=sendstring+   "Session ID:  " +gSessionIdentifier+   "\n" ;
      
     sendstring=sendstring+   "Session Start Time:  "    +String(timeObject.getYear())+"-"  +String(timeObject.getMonth())+"-" +String(timeObject.getDay())+" " +String(timeObject.getHour(true))+":" +String(timeObject.getMinute())+":"  +String(timeObject.getSecond())           + "\n" ;

          
      sendstring=sendstring+   "Firmware Version:  "+          gFirmwareVersion                    + "\n" ; //firmware
      
    
      sendstring=sendstring+   "File Header:  "+     gLocation                         + "\n" ; //file header
  
      sendstring=sendstring+   "Bluetooh on when Record?:   " +getMicInfo().MicBluetoothOnOrOff              + "\n" ;
  
      sendstring=sendstring+   "Sample Rate:  " +String(gSampleRate)               + "\n" ;
      sendstring=sendstring+   "Seconds Per File:  " +String(gSecondsPerFile)               + "\n" ;
 
      
  
       //sendstring=sendstring+   "Voltage Offset:  " +String(gVoltageOffset)                  + "\n" ;
       sendstring=sendstring+   "Mic Type:  " +getMicInfo().MicType                  + "\n" ;
        sendstring=sendstring+   "SD Card Free GB:   "+ String(gFreeSpaceGB)                  + "\n" ;
       sendstring=sendstring+   "Mic Gain:  " +getMicInfo().MicBitShift                  + "\n" ;
       sendstring=sendstring+   "GPS Location:  " +gLocationCode                + "\n" ;
      sendstring=sendstring+    "GPS Accuracy:  " +gLocationAccuracy                + " m\n" ;
     
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

void sendElocStatus() {  //compiles and sends eloc config
      /*
        

        will be based on 



      //what kind of things we want to send?
      // distinguish between record now,  no-record and previous record
      - mac address
      - gfirmwareVersion
      - eloc_name
      - android appver
      - timeofday (phone time)
      - ID of ranger who did it
      - bat voltage
      - o sdcard size
      - sdcard free space
      - gLocation
      - gps location

      /// mic info
        - gMicType
        - 


      - buffer underruns etc of last record


      // info of last record session: ?
          -buffer underruns
          - record time
          - record type , samplerate, gain, etc
          - max/avg file write time.



      ///////// timings since last boot ////
          - total uptime  
          - record time no bluetooth
          - record time bluetooth

      /////// timing since last bat change ////
          - total uptime  
          - record time no bluetooth
          - record time bluetooth 


      //// if recording was started  ///
          - time of day
          - secondsperfile
          - samplerate
          - gMicBitShift
          - gps coords
          - gps accuracy
          - record type (bluetooth on or off)
          - other record parameters, e.g. cpu freq, apll, etc buffer sizes?
          


      */ 


       String sendstring= "statusupdate\n";
       sendstring=sendstring+   "Time:  "    +getProperDateTime()          + "\n" ;
       sendstring=sendstring+  "Ranger:  "    +"_@b$_"           + "\n" ;
       //sendstring=sendstring+ "\n\n";
       sendstring=sendstring+   "!0!"+          readNodeName()                     + "\n" ; //dont change

      
      sendstring=sendstring+   "!1!"+          gFirmwareVersion                    + "\n" ; //firmware
      
      float tempvolts= Battery::GetInstance().getVoltage();
      String temptemp= "FULL";
      if (!Battery::GetInstance().isFull()) temptemp="";
      if (Battery::GetInstance().isLow()) temptemp="!!! LOW !!!";
      if (Battery::GetInstance().isEmpty()) temptemp="turn off";

      if (Battery::GetInstance().isCalibrationDone()) {
          sendstring=sendstring+   "!2!" +String(tempvolts)+ " v # "+temptemp+"\n" ;                     //battery voltage
      } else {
           sendstring=sendstring+   "!2!" +String(tempvolts)+ " v "+temptemp+"\n" ;  
      }
      sendstring=sendstring+   "!3!"+     gLocation                         + "\n" ; //file header
      
  
       //was uint64tostring
      sendstring=sendstring+   "!4!"    +      String((float)esp_timer_get_time()/1000/1000/60/60)    + " h"           + "\n" ;
      sendstring=sendstring+   "!5!"      +    String(((float)gTotalRecordTimeSinceReboot+gSessionRecordTime)/1000/1000/60/60)  +" h"           + "\n" ;
      sendstring=sendstring+   "!6!"      +    String((float)gSessionRecordTime/1000/1000/60/60 )  +" h"           + "\n" ;
        
       
       
       sendstring=sendstring+   "!7!" +String(gRecording)              + "\n" ;
  
      sendstring=sendstring+   "!8!" +getMicInfo().MicBluetoothOnOrOff              + "\n" ;
  
      sendstring=sendstring+   "!9!" +String(gSampleRate)               + "\n" ;
      sendstring=sendstring+   "!10!" +String(gSecondsPerFile)               + "\n" ;
 
      
  
       sendstring=sendstring+   "!11!"+ String(gFreeSpaceGB)                  + "\n" ;
       sendstring=sendstring+   "!12!" +getMicInfo().MicType                  + "\n" ;
  
       sendstring=sendstring+   "!13!" +getMicInfo().MicBitShift                  + "\n" ;
       sendstring=sendstring+   "!14!" +gLocationCode                + "\n" ;
      sendstring=sendstring+   "!15!" +gLocationAccuracy                + " m\n" ;

     
      sendstring=sendstring+   "!16!"+gSessionIdentifier                         + "\n" ;

      Serial.print(sendstring);
      btwrite(sendstring);
      
} 




void app_main(void)
{  
  ESP_LOGI(TAG, "\n\n---------VERSION %s\n\n", VERSIONTAG);
  initArduino();
  ESP_LOGI(TAG, "initArduino done");
  
  ESP_LOGI(TAG, "\nSETUP--start\n");

  changeBootPartition(); // so if reboots, always boot into the bluetooth partition
 
 printRevision();

resetPeripherals();
 
I2SSampler *input;

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
      
      //new
      gpio_set_direction(GPIO_BUTTON, GPIO_MODE_INPUT);   //
      gpio_set_pull_mode(GPIO_BUTTON, GPIO_PULLUP_ONLY);
      //end new
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

       //new
      gpio_set_direction(GPIO_BUTTON, GPIO_MODE_INPUT);   //
      gpio_set_pull_mode(GPIO_BUTTON, GPIO_PULLUP_ONLY);
      //end new
      
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

ESP_LOGI(TAG, "Setting up HW System...");
ElocSystem::GetInstance();

ESP_LOGI(TAG, "Setting up Battery...");
Battery::GetInstance();


//delay(30000);

printMemory();

  
  if(!SPIFFS.begin(true, "/spiffs")){
      ESP_LOGI(TAG, "An Error has occurred while mounting SPIFFS");
      //return;
  }
  delay(50);
//resetPeripherals(); does not help.
delay(50);
SerialBT.begin(readNodeName(),false);
delay(100);
if (SerialBT.isReady())  {
      ESP_LOGI(TAG, "SerialBT is ready ------------------------------------------------------ ");
} else {
       ESP_LOGI(TAG, "SerialBT is NOT ready --------------------------------------------------- ");
}


readSettings();
readMicInfo();

ESP_ERROR_CHECK(gpio_install_isr_service(GPIO_INTR_PRIO));

ESP_LOGI(TAG, "Creating LIS3DH wakeup task...");
if (esp_err_t err = ManualWakeupConfig(false)) {
  ESP_LOGI(TAG, "ManualWakeupConfig %s", esp_err_to_name(err));
}

 
ESP_ERROR_CHECK(gpio_isr_handler_add(GPIO_BUTTON, buttonISR, (void *)GPIO_BUTTON));
ESP_ERROR_CHECK(gpio_isr_handler_add(GPIO_BUTTON, buttonISR, (void *)OTHER_GPIO_BUTTON));

  bool firstRecordLoopAlreadyStarted=false;
  //BUGME: remove this loop!
  while (true) 
  {
      
      while (firstRecordLoopAlreadyStarted) { //ugly hack
          delay(5);

      }
      
      firstRecordLoopAlreadyStarted=true;

  
  mountSDCard();
      
  readConfig();

       esp_pm_config_esp32_t cfg = {
        .max_freq_mhz = gMaxFrequencyMHZ,
        .min_freq_mhz = gMinFrequencyMHZ,
        .light_sleep_enable = gEnableLightSleep
        };
  esp_pm_configure(&cfg);   

 


    input = new I2SMEMSSampler(I2S_NUM_0, i2s_mic_pins, i2s_mic_Config,gTimingFix); //the true at the end is the timing fix
    if (TestI2SClockInput) testInput();


    
    while (true) {   
      wait_for_button_push();
      
      record(input);
      vTaskDelay(pdMS_TO_TICKS(500));
    }
 
  }
}
