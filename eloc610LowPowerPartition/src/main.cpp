
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
#include "WAVFileWriter.h"
//#include "WAVFileReader.h"
#include "config.h"
#include <string.h>

#include "esp_partition.h"
#include "esp_ota_ops.h"

#include <time.h>
#include <sys/time.h>

#include "esp_sleep.h"
#include "rtc_wdt.h"
//#include "soc/efuse_reg.h"
static const char *TAG = "main";


bool TestI2SClockInput=false;
bool gMountedSDCard=false;
bool gRecording=false;
bool gGPIOButtonPressed =false;


int32_t *graw_samples;

int gRawSampleBlockSize=1000;
int gSampleBlockSize=16000; //must be factor of 1000   in samples
int gBufferLen=1000; //in samples
int gBufferCount=18;   // so 6*4000 = 24k buf len

////// these are the things you can change for now. 

uint32_t gSampleRate;
uint32_t  gRealSampleRate;
int  gbitShift; //can be 11 through 14 11 for far-field (default) listening and 14 for mounted on mahout.
uint64_t gStartupTime; //gets read in at startup to set tystem time. 
int gSecondsPerFile= 60;

char gSessionFolder[65];

//voltage
//bool gVoltageCalibrationDone=false;
float gVoltageOffset=10.0; //read this in on startup
float gvOff=2.7;
float gvFull=3.3;
float gvLow=3.18;
int gMinutesWaitUntilDeepSleep=60; //change to 1 or 2 for testing

bool gTimingFix=false;
bool gListenOnly=false;
bool gUseAPLL=true;
int gMaxFrequencyMHZ=80;    // SPI this fails for anyting below 80   //
int gMinFrequencyMHZ=10;
bool gEnableLightSleep=true; //only for AUTOMATIC light leep.

 #ifdef USE_SPI_VERSION
    SDCard *theSDCardObject;
 #endif

#ifdef USE_SDIO_VERSION
    SDCardSDIO *theSDCardObject;
 #endif


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


void wait_for_button_push()
{
  
  
  
   ESP_LOGI(TAG, "\n\n\n");
   ESP_LOGI(TAG, "-----Waiting for GPIO Button Press to Mount SD Card and start recording ---------");
    ESP_LOGI(TAG, "\n\n\n");
  while (gpio_get_level(GPIO_BUTTON) == 1)
  {
    vTaskDelay(pdMS_TO_TICKS(100));
  }
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


float IRAM_ATTR getVoltage() {
    //Statements;
    //Serial.println(" two following are heap size, total and max alloc ");
    // Note: ADC2 pins cannot be used when Wi-Fi is used. So, if you’re using Wi-Fi and you’re having trouble 
    //getting the value from an ADC2 GPIO, you may consider using an ADC1 GPIO instead, that should solve your problem.
    
    
    // we want to measure up to 4 volts. 
    //ed's stuff    https://www.youtube.com/watch?v=5srvxIm1mcQ 470k      1.47meg

//so the new vrange = 3.96v 


//3.25v on en pin corresponds to 1.83v on gpio34
//so voltage = pinread/4095 *X   where x is the multiplier varies from device to device and on input current.
//3.29v =2979 
//so 3.29= 2979/4095 *3.96*X            3.29 =2.88*X   so X = 1.142
    //pinMode(VOLTAGE_PIN,INPUT);
    //analogSetPinAttenuation(VOLTAGE_PIN, ADC_11db);
     //vTaskDelay(pdMS_TO_TICKS(500));
      //analogRead(VOLTAGE_PIN)
    
     // return(2.6);
    
     //uint16_t value=analogRead(VOLTAGE_PIN);

     float accum=0.0; 
     float avg;
     for (int i=0;  i<5;i++) {
       accum+= gpio_get_level(VOLTAGE_PIN);
     } 
      //return((float)accum/5.0);
      avg=accum/5.0;
      
      
      // Serial.print("voltage raw, calc" );
      //  Serial.print(value);
      //   Serial.print("    ");
      //  Serial.print(((float)value/4095)*3.96*1.142 ); //see above calc
      //   Serial.println("    ");
         
       //Serial.println(analogReadMilliVolts(VOLTAGE_PIN));
       //delay(500);

   return(10.0);                    //comment
  //return((avg/4095)*3.96*1.142); //uncomment for field versions

}

void doDeepSleep(){
   
       esp_sleep_enable_ext0_wakeup(OTHER_GPIO_BUTTON, 0); //then try changing between 0 and 1.

      esp_sleep_enable_ext0_wakeup(GPIO_BUTTON, 0); //try commenting this out
     
      
     // Serial.println("Going to sleep now");
      delay(2000);
      esp_deep_sleep_start(); //change to deep?  
      
      //Serial.println("OK button was pressed.waking up");
      delay(2000);
      esp_restart();


}











void record(I2SSampler *input) {
  
  gRecording=true;
  char fname[100];

    input->start();
  gRealSampleRate=(int32_t)(i2s_get_clk(I2S_NUM_0));
  ESP_LOGI(TAG, "I2s REAL clockrate in record  %ld", gRealSampleRate  );

  
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
      //Serial.print("loops: "); Serial.println(loops); 
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
             
                //Serial.println("freeSpaceGB "+String(gFreeSpaceGB));

                recordupdate=esp_timer_get_time(); 

                //if (gGPIOButtonPressed)  {stopit=true; loopCounter=10000001L; ESP_LOGI(TAG,"gpio button pressed");}
               // ok fix me put me back in if (gFreeSpaceGB<0.2f)  {stopit=true; loopCounter=10000001L; }
                
                 //voltage check
                if ((loopCounter % 50)==0 ) {
                   ESP_LOGI(TAG, "LOOPCOUNTER MOD 50 is 0" );
                   if ((getVoltage()+gVoltageOffset)<gvOff) {
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
    #endif



}





void app_main(void)
{
  changeBootPartition(); // so if reboots, always boot into the bluetooth partition
  ESP_LOGI(TAG, "\n\n---------VERSION %s\n\n", VERSION);
 
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



//delay(30000);

printMemory();

  
  new SPIFFS("/spiffs");
  char line[128]="";
  char *position;
 
  FILE *f = fopen("/spiffs/currentsession.txt", "r");
  if (f == NULL) {
        //gSessionFolder="";
        ESP_LOGI(TAG, "Failed to open file for reading");
        //return;
    } else {

 

            //char *test=line;

          
            while (!feof(f)) {
              fgets(line, sizeof(line), f);
              //ESP_LOGI(TAG, "%s", line);
              //char *test2;
              //int start,finish;
              position = strstr (line,"Session ID");
              if (position != NULL) {
                    position = strstr (line,"_"); //need to ad one memory location
                    //
                    // the long epoch will be in the first 10 digits. int microseconds the last 3 
                    
                    gStartupTime= atoll(position+1);

                    char epoch[10]="";
                    epoch[0]=*(position+1);
                    epoch[1]=*(position+2);
                    epoch[2]=*(position+3);
                    epoch[3]=*(position+4);
                    epoch[4]=*(position+5);
                    epoch[5]=*(position+6);
                    epoch[6]=*(position+7);
                    epoch[7]=*(position+8);
                    epoch[8]=*(position+9);
                    epoch[9]=*(position+10);
                    
                    
                    
                    char ms[3]="";
                    ms[0]=*(position+11);
                    ms[1]=*(position+12);
                    ms[2]=*(position+13);

                    //ESP_LOGI(TAG, "epoch %s",  epoch);
                    //ESP_LOGI(TAG, "ms %s",  ms);
                    
                    setTime(atol(epoch),atoi(ms));
                    ESP_LOGI(TAG, "startup time %lld",  gStartupTime);
                    //ESP_LOGI(TAG, "testing atol %ld",  atol("1676317685250"));
                    // ESP_LOGI(TAG, "testing atoll %lld",  atoll("1676317685250"));
                    position = strstr (line,":  ");
                    ESP_LOGI(TAG, "session FOLDER 1 %s",  position+3);
                    strncat(gSessionFolder,position+3,63);
                    gSessionFolder[strcspn(gSessionFolder, "\n")] = '\0';
                    ESP_LOGI(TAG, "gSessionFolder %s",  gSessionFolder);


              }
              position = strstr (line,"Sample Rate:");
              if (position != NULL) {
                    position = strstr (line,":  "); //need to ad one memory location
                    gSampleRate=atoi(position+3);
                    //gSampleRate=4000;
                    ESP_LOGI(TAG, "sample rate %ld", gSampleRate );
        

              }
        
              position = strstr (line,"Mic Gain:");
              if (position != NULL) {
                    position = strstr (line,":  "); //need to ad one memory location
                    gbitShift=atol(position+3);
                    ESP_LOGI(TAG, "bit shift %d", gbitShift );
        

              }

              position = strstr (line,"Seconds Per File:");
              if (position != NULL) {
                    position = strstr (line,":  "); //need to ad one memory location
                    gSecondsPerFile=atol(position+3);
                    ESP_LOGI(TAG, "seconds per file %d", gSecondsPerFile );
        

              }
        
              
            }
    }
    
   fclose(f);
  
    f = fopen("/spiffs/voltageoffset.txt", "r");
    if (f == NULL) {
          ESP_LOGI(TAG, "no voltage offset");
          //return;
    } else {
      fgets(line, sizeof(line), f);
      gVoltageOffset=atof(line);
      ESP_LOGI(TAG, "voltage offset %f", gVoltageOffset );
     


    }

    fclose(f);


   
 


 
 
ESP_ERROR_CHECK(gpio_install_isr_service(0));
ESP_ERROR_CHECK(gpio_isr_handler_add(GPIO_BUTTON, buttonISR, (void *)GPIO_BUTTON));
ESP_ERROR_CHECK(gpio_isr_handler_add(GPIO_BUTTON, buttonISR, (void *)OTHER_GPIO_BUTTON));

  bool firstRecordLoopAlreadyStarted=false;
  while (true)
  {
      
      while (firstRecordLoopAlreadyStarted) { //ugly hack
          delay(5);

      }
      
      firstRecordLoopAlreadyStarted=true;


       esp_pm_config_esp32_t cfg = {
      .max_freq_mhz = gMaxFrequencyMHZ,
      .min_freq_mhz = gMinFrequencyMHZ,
      .light_sleep_enable = gEnableLightSleep
  };
  esp_pm_configure(&cfg);   

 
  
  mountSDCard();
      


    if (gMountedSDCard) {
          f = fopen("/sdcard/eloctest.txt", "r");
          if (f == NULL) {
          ESP_LOGI(TAG, "no eloc testing file on root of SDCARD ");
          //return;
        } else {
           ESP_LOGI(TAG, "\n\n\n");
            while (!feof(f)) {
                  fgets(line, sizeof(line), f);
                  //ESP_LOGI(TAG, "%s", line);
                  
                  
                  position = strstr (line,"SampleRate:");
                  if (position != NULL) {
                      position = strstr (line,":");
                      gSampleRate=atoi(position+1);
                      ESP_LOGI(TAG, "sample rate override %ld", gSampleRate );
                  }

                  position = strstr (line,"MaxFrequencyMHZ:");
                  if (position != NULL) {
                      position = strstr (line,":");
                      gMaxFrequencyMHZ=atoi(position+1);
                      ESP_LOGI(TAG, "Max Frequency MHZ override %d", gMaxFrequencyMHZ );
                  }
                 position = strstr (line,"MinFrequencyMHZ:");
                  if (position != NULL) {
                      position = strstr (line,":");
                      gMinFrequencyMHZ=atoi(position+1);
                      ESP_LOGI(TAG, "Min Frequency MHZ override %d", gMinFrequencyMHZ );
                  }

                position = strstr (line,"gain:");
                  if (position != NULL) {
                      position = strstr (line,":");
                      gbitShift=atoi(position+1);
                      ESP_LOGI(TAG, "gain override: %d", gbitShift );
                  }

                position = strstr (line,"VoltageOffset:");
                  if (position != NULL) {
                      position = strstr (line,":");
                      gVoltageOffset=atof(position+1);
                      ESP_LOGI(TAG, "voltage offset  override: %f", gVoltageOffset );
                  }

                position = strstr (line,"SecondsPerFile:");
                  if (position != NULL) {
                      position = strstr (line,":");
                      gSecondsPerFile=atoi(position+1);
                      ESP_LOGI(TAG, "Seconds per File override: %d", gSecondsPerFile );
                  }


                  position = strstr (line,"UseAPLL:no");if (position != NULL) {gUseAPLL=false;}
                  position = strstr (line,"UseAPLL:yes");if (position != NULL) {gUseAPLL=true;}
                  position = strstr (line,"TryLightSleep?:yes");if (position != NULL) {gEnableLightSleep=true;}
                  position = strstr (line,"TryLightSleep?:no");if (position != NULL) {gEnableLightSleep=false;}
                  position = strstr (line,"ListenOnly?:no");if (position != NULL) {gListenOnly=false;}
                  position = strstr (line,"ListenOnly?:yes");if (position != NULL) {gListenOnly=true;}
                  position = strstr (line,"TimingFix:no");if (position != NULL) {gTimingFix=false;}
                  position = strstr (line,"TimingFix:yes");if (position != NULL) {gTimingFix=true;}
                  position = strstr (line,"TestI2SClockInput:no");if (position != NULL) {TestI2SClockInput=false;}
                  position = strstr (line,"TestI2SClockInput:yes");if (position != NULL) {TestI2SClockInput=true;}
                    



            }
            ESP_LOGI(TAG, "Use APLL override: %d", gUseAPLL );
            ESP_LOGI(TAG, "Enable light Sleep override: %d", gEnableLightSleep );
            ESP_LOGI(TAG, "Listen Only override: %d", gListenOnly );
            ESP_LOGI(TAG, "Timing fix override: %d", gTimingFix );
            ESP_LOGI(TAG, "Test i2sClockInput: %d", TestI2SClockInput );
            ESP_LOGI(TAG, "\n\n\n");

            fclose(f);

        }

    }  

 
    i2s_mic_Config.sample_rate=gSampleRate;
    if (gSampleRate<=32000) { //my wav files sound wierd if apll clock raate is > 32kh. So force non-apll clock if >32khz
      i2s_mic_Config.use_apll=gUseAPLL;
        ESP_LOGI(TAG, "Sample Rate is < 32khz USE APLL Clock %d", gUseAPLL );
    } else {
      i2s_mic_Config.use_apll=false;
      ESP_LOGI(TAG, "Sample Rate is > 32khz Forcing NO_APLL " );
    }
    input = new I2SMEMSSampler(I2S_NUM_0, i2s_mic_pins, i2s_mic_Config,gTimingFix); //the true at the end is the timing fix
    if (TestI2SClockInput) testInput();
    if (gListenOnly) {
       delete theSDCardObject;
       record(input);
       resetESP32();

    }

     if (gMountedSDCard) {
         record(input);
         delete theSDCardObject;
         resetESP32();
     } else {
        LEDflashError();
        delay(2000);
        delete theSDCardObject;
        resetESP32();

     }


  
  }
}
