


#include <sys/time.h>
#include <stdlib.h>
#include <Arduino.h>
#include <stdio.h>
#include <FreeRTOS.h>
#include <I2SMEMSSampler.h>
//#include <ADCSampler.h>
#include <esp_bt.h>
#include <esp_bt_main.h>
#include <esp_bt_device.h>
#include <sys/stat.h>
//#include <sys/>
//#include <sys/dirent.h>
//new stuff for ota
//#include <WiFi.h>
//#include <WiFiClient.h>
//#include <WebServer.h>
//#include <ESPmDNS.h>
#include <Update.h>
// end ota stuff

//#include <WiFi.h>
//#include <I2SOutput.h>
//#include <DACOutput.h>
#include <SDCard.h>
#include "esp_vfs_fat.h"
#include <SDCardSDIO.h>
#include "SPIFFS.h"
//#include <WAVFileReader.h>
#include <WAVFileWriter.h>
#include <BluetoothSerial.h>
//#include <sys/time.h>
#include "ESP32Time.h"
//#include <string>
//new
//#include <FS.h>

#include <FFat.h>
#include <SD.h>
//#include "esp_vfs_fat.h"
//#include <SPI.h>
//end new

//#include <WiFi.h>
//#include <WiFiClient.h>
//#include <WebServer.h>
//#include <ESPmDNS.h>

#include "config.h"
#include "soc/rtc.h"
//#include  "rom/rtc.h"
//#include "driver/adc.h"
//#include "esp_adc_cal.h"
//#define ADC_EXAMPLE_CALI_SCHEME  ESP_ADC_CAL_VAL_EFUSE_VREF
//esp_adc_cal_characteristics_t gadc1_chars;

#include "esp_partition.h"
#include "esp_ota_ops.h"
#include "soc/rtc_wdt.h"

//global variables for reset
//Power on reset does not preserve RTC RAM.
//A software reset will preserve NOINIT variables in RTC RAM but will initialize data variables with initial data and bss with zeroes.



// global variables to send to the other partition
RTC_NOINIT_ATTR uint32_t testNoinit=35;
RTC_NOINIT_ATTR uint32_t testNoinit2=244;

RTC_NOINIT_ATTR esp_reset_reason_t gLastResetReason;


int32_t gRealSampleRate=0;
int gBufferLen=1000; //in samples
int gBufferCount=12;   // so 8*1000 = 8k buf len
int gSampleBlockSize=16000; //must be factor of 1000
//int gSampleBlockSizeBluetooth=16000;
//int gSampleBlockSizeNoBluetooth=16000;

float gVoltage[] = {0.0f, 0.0f, 0.0f, 0.0f};
int32_t *graw_samples;
int gSampleRate=16000;
int gSecondsPerFile= 10;
//always keep these in same order
String gLocation = "not_set";
String gMicType="ns";
String gMicBitShift="11";
int gbitShift;
String gMicGPSCoords="ns";
String gMicPointingDirectionDegrees="ns";
String gMicHeight="ns";
String gMicMountType="ns";
String gMicBluetoothOnOrOff="on";
bool gDisableBluetoothWhenRecording=false;

bool gMountedSDCard = false;

RTC_NOINIT_ATTR bool gRecording;    //set these two in waitforbutt
RTC_NOINIT_ATTR bool gResumeRecordOnReboot;

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
DWORD  gFreeSpaceKB=0;
long gLastSystemTimeUpdate; // local system time of last time update PLUS minutes since last phone update 
String gSyncPhoneOrGoogle; //will be either G or P (google or phone).
String gLocationCode="unknown";
String gLocationAccuracy="99";
//String gTimeDifferenceCode; //see getTimeDifferenceCode() below
bool gBlueToothISDisabled=false;

//timing
int64_t gTotalUPTimeSinceReboot=esp_timer_get_time();  //esp_timer_get_time returns 64-bit time since startup, in microseconds.
int64_t gTotalRecordTimeSinceReboot=0;
int64_t gSessionRecordTime=0;

//float gVoltage;
bool gVoltageCalibrationDone=false;
float gVoltageOffset=10.0; //read this in on startup
float gvOff=2.7;
float gvFull=3.3;
float gvLow=3.18;
int gMinutesWaitUntilDeepSleep=60; //change to 1 or 2 for testing

//session stuff
String gSessionIdentifier="";

 #ifdef USE_SPI_VERSION
    SDCard *theSDCardObject;
 #endif

#ifdef USE_SDIO_VERSION
    SDCardSDIO *theSDCardObject;
 #endif




void resetPeripherals() {
   periph_module_reset( PERIPH_I2S0_MODULE);   //bug see here: https://github.com/espressif/esp-idf/issues/9971
    periph_module_reset( PERIPH_BT_MODULE);
    periph_module_reset( PERIPH_WIFI_BT_COMMON_MODULE);
    periph_module_reset( PERIPH_BT_MODULE);
    periph_module_reset(  PERIPH_BT_BASEBAND_MODULE);
    periph_module_reset(PERIPH_BT_LC_MODULE);
    //periph_module_reset(PERIPH_MODULE_MAX);
    //wifi_bt_common_module_disable();

}



void resetESP32() {
        
      //delete theSDCardObject;
      //gMountedSDCard=false;
     

    
    ESP_LOGI(TAG, "Resetting in 100 ms");
    rtc_wdt_protect_off();      //Disable RTC WDT write protection
    
    rtc_wdt_set_stage(RTC_WDT_STAGE0, RTC_WDT_STAGE_ACTION_RESET_SYSTEM);
    rtc_wdt_set_time(RTC_WDT_STAGE0, 100);
    rtc_wdt_enable();           //Start the RTC WDT timer
    rtc_wdt_protect_on();       //Enable RTC WDT write protection
    delay(500);


}





void changeBootPartition(int partitionNumber) {  
  //bool found=false;
    const esp_partition_t *partition;
    if (partitionNumber==0) {
         // found=true;
         partition = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, "partition0");
         if (partition==NULL) {
           ESP_LOGE(TAG, "\npartition0 not found. Are you sure you built/flased it correctly? \n");
            delay(2000);
          } else { 

              esp_ota_set_boot_partition(partition);
            ESP_LOGI(TAG, "changed boot partition to  partition0.");
        }

    
    }

       if (partitionNumber==1) {
         // found=true;
         partition = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, "partition1");
         if (partition==NULL) {
           ESP_LOGE(TAG, "\npartition1 not found. Are you sure you built/flased it correctly? \n");
            delay(2000);
          } else { 

              esp_ota_set_boot_partition(partition);
            ESP_LOGI(TAG, "changed boot partition to  partition1.");
        }

    
    }


      if (partitionNumber==2) {
         // found=true;
         partition = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, "update");
         if (partition==NULL) {
           ESP_LOGE(TAG, "\nupdate partition not found. Are you sure you built/flased it correctly? \n");
            delay(2000);
          } else { 

              esp_ota_set_boot_partition(partition);
            ESP_LOGI(TAG, "changed boot partition to  update.");
        }

    
    }



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
     
      
      Serial.println("Going to sleep now");
      delay(2000);
      esp_light_sleep_start(); //change to deep?  
      
      Serial.println("OK button was pressed.waking up");
      delay(2000);
      ESP.restart();


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
       accum+= analogRead(VOLTAGE_PIN);
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




float calculateVoltageOffset() {
  //gvoltageoffset will always be ADDED
  // assume battery is currently at voff =2.7
  // 
  float temp= getVoltage()-gvLow; //if v = 2.6 offset will be neg so need to be added  if 2.8, pos, so need to be sub 
  gVoltageOffset=temp*-1.0;
  Serial.println ("voltage offset is "+String(gVoltageOffset));
 
}











void print_reset_reason()
{
  
   esp_reset_reason_t reason;
  reason= esp_reset_reason();
  switch ( reason)
  {
    case 1 : Serial.println ("POWERON_RESET");break;          /**<1,  Vbat power on reset*/
    case 3 : Serial.println ("SW_RESET");break;               /**<3,  Software reset digital core*/
    case 4 : Serial.println ("OWDT_RESET");break;             /**<4,  Legacy watch dog reset digital core*/
    case 5 : Serial.println ("DEEPSLEEP_RESET");break;        /**<5,  Deep Sleep reset digital core*/
    case 6 : Serial.println ("SDIO_RESET");break;             /**<6,  Reset by SLC module, reset digital core*/
    case 7 : Serial.println ("TG0WDT_SYS_RESET");break;       /**<7,  Timer Group0 Watch dog reset digital core*/
    case 8 : Serial.println ("TG1WDT_SYS_RESET");break;       /**<8,  Timer Group1 Watch dog reset digital core*/
    case 9 : Serial.println ("RTCWDT_SYS_RESET");break;       /**<9,  RTC Watch dog Reset digital core*/
    case 10 : Serial.println ("INTRUSION_RESET");break;       /**<10, Instrusion tested to reset CPU*/
    case 11 : Serial.println ("TGWDT_CPU_RESET");break;       /**<11, Time Group reset CPU*/
    case 12 : Serial.println ("SW_CPU_RESET");break;          /**<12, Software reset CPU*/
    case 13 : Serial.println ("RTCWDT_CPU_RESET");break;      /**<13, RTC Watch dog Reset CPU*/
    case 14 : Serial.println ("EXT_CPU_RESET");break;         /**<14, for APP CPU, reseted by PRO CPU*/
    case 15 : Serial.println ("RTCWDT_BROWN_OUT_RESET");break;/**<15, Reset when the vdd voltage is not stable*/
    case 16 : Serial.println ("RTCWDT_RTC_RESET");break;      /**<16, RTC Watch dog reset digital core and rtc module*/
    default : Serial.println ("NO_MEAN");
  }
}





int64_t getSystemTimeMS() {
             struct timeval tv_now;
            gettimeofday(&tv_now, NULL);
            int64_t time_us = (     (int64_t)tv_now.tv_sec      * 1000000L) + (int64_t)tv_now.tv_usec;
            time_us=time_us/1000;
          return(time_us);


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

// void timeUpdates() {
//     gTotalUPTimeSinceReboot=esp_timer_get_time();
//     if (gRecording)  {
//      //gTotalRecordTimeSinceReboot= gTotalRecordTimeSinceReboot+

//     }
//     //Serial.println("total uptime since reboot "+uint64ToString(gTotalUPTimeSinceReboot/1000L/1000L)+ " seconds");

// }


String getTimeDifferenceCode(long elapsedTimeDifferenceMinutes) {
    String Y="A"; //less than one minute since last time sync
    if(elapsedTimeDifferenceMinutes>1) Y="B"; //between 1 and 5 minutes
    if(elapsedTimeDifferenceMinutes>10) Y="C"; //etc
    if(elapsedTimeDifferenceMinutes>60) Y="D";
    if(elapsedTimeDifferenceMinutes>6*60) Y="E";
    if(elapsedTimeDifferenceMinutes>12*60) Y="F";
    if(elapsedTimeDifferenceMinutes>24*1*60) Y="G";
    if(elapsedTimeDifferenceMinutes>24*3*60) Y="H";
    if(elapsedTimeDifferenceMinutes>24*4*60) Y="I";
    if(elapsedTimeDifferenceMinutes>24*5*60) Y="J";
    if(elapsedTimeDifferenceMinutes>24*6*60) Y="K";
    if(elapsedTimeDifferenceMinutes>24*7*60) Y="L";
    if(elapsedTimeDifferenceMinutes>24*8*60) Y="M";
    if(elapsedTimeDifferenceMinutes>24*9*60) Y="N";
    if(elapsedTimeDifferenceMinutes>24*10*60) Y="O";
    if(elapsedTimeDifferenceMinutes>24*11*60) Y="P";
    if(elapsedTimeDifferenceMinutes>24*12*60) Y="Q";
    if(elapsedTimeDifferenceMinutes>24*13*60) Y="R";
    if(elapsedTimeDifferenceMinutes>24*14*60) Y="S";
    if(elapsedTimeDifferenceMinutes>24*15*60) Y="T";
    if(elapsedTimeDifferenceMinutes>24*16*60) Y="U";
    if(elapsedTimeDifferenceMinutes>24*17*60) Y="V";
    if(elapsedTimeDifferenceMinutes>24*18*60) Y="W";
    if(elapsedTimeDifferenceMinutes>24*19*60) Y="X";
    if(elapsedTimeDifferenceMinutes>24*20*60) Y="Y";
    if(elapsedTimeDifferenceMinutes>24*21*60) Y="Z";
    return(Y);
    // etc etc etc etc etc etc etc etc etc etc etc  until z  

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
 FATFS *fs;
    DWORD fre_clust, fre_sect, tot_sect;
    FRESULT res;
    /* Get volume information and free clusters of drive 0 */
    res = f_getfree("0:", &fre_clust, &fs);
    /* Get total sectors and free sectors */
    tot_sect = (fs->n_fatent - 2) * fs->csize;
    fre_sect = fre_clust * fs->csize;

    /* Print the free space (assuming 512 bytes/sector) */
    Serial.printf("%10lu KiB total drive space.\n%10lu KiB available.\n",
           tot_sect / 2, fre_sect / 2);


      gFreeSpaceGB= float((float)fre_sect/1048576.0 /2.0);
      Serial.println(" ");
      Serial.print(gFreeSpaceGB); 
      Serial.println(" GB free");

      gFreeSpaceKB=fre_sect / 2;

      Serial.println(" ");
      Serial.print(gFreeSpaceKB); 
      Serial.println(" KB free");

}







void LEDflashError() {
      for (int i=0;i<20;i++){
        digitalWrite(STATUS_LED,HIGH);
        delay(50);
        digitalWrite(STATUS_LED,LOW);
        delay(50);
    }  
}


bool folderExists(const char* folder)
{
    
    
    //folder = "/sdcard/eloc";
    struct stat sb;

    if (stat(folder, &sb) == 0 && S_ISDIR(sb.st_mode)) {
        Serial.println("yes");
         return true;
    } else {
         Serial.println("no");
         return false;
    }

}


String readNodeName() {

      // int a =gDeleteMe.length();
      // if (a==2) {a=1;}
      if(!(SPIFFS.exists("/nodename.txt"))){
  
        Serial.println("No nodename set. Returning ELOC_NONAME");
        return("ELOC_NONAME");
          

      }
 

  File file2 = SPIFFS.open("/nodename.txt", FILE_READ);
  
  //String temp = file2.readStringUntil('\n');

  String temp = file2.readString();
  temp.trim();
  file2.close();
  Serial.println("node name: "+temp);
  return(temp);
 //return("");

}




void saveStatusToSD() {
       String sendstring;
       
      sendstring=sendstring+   "Session ID:  " +gSessionIdentifier+   "\n" ;
      
     sendstring=sendstring+   "Session Start Time:  "    +String(timeObject.getYear())+"-"  +String(timeObject.getMonth())+"-" +String(timeObject.getDay())+" " +String(timeObject.getHour(true))+":" +String(timeObject.getMinute())+":"  +String(timeObject.getSecond())           + "\n" ;

          
      sendstring=sendstring+   "Firmware Version:  "+          gFirmwareVersion                    + "\n" ; //firmware
      
    
      sendstring=sendstring+   "File Header:  "+     gLocation                         + "\n" ; //file header
      
  
       
       
       
     
  
      sendstring=sendstring+   "Bluetooh on when Record?:   " +gMicBluetoothOnOrOff              + "\n" ;
  
      sendstring=sendstring+   "Sample Rate:  " +String(gSampleRate)               + "\n" ;
      sendstring=sendstring+   "Seconds Per File:  " +String(gSecondsPerFile)               + "\n" ;
 
      
  
       //sendstring=sendstring+   "Voltage Offset:  " +String(gVoltageOffset)                  + "\n" ;
       sendstring=sendstring+   "Mic Type:  " +gMicType                  + "\n" ;
        sendstring=sendstring+   "SD Card Free GB:   "+ String(gFreeSpaceGB)                  + "\n" ;
       sendstring=sendstring+   "Mic Gain:  " +gMicBitShift                  + "\n" ;
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
      
      float tempvolts= getVoltage()+gVoltageOffset;
      String temptemp= "FULL";
      if (tempvolts <gvFull) temptemp="";
      if (tempvolts <gvLow) temptemp="!!! LOW !!!";
      if (tempvolts <gvOff) temptemp="turn off";

      if (gVoltageCalibrationDone) {
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
  
      sendstring=sendstring+   "!8!" +gMicBluetoothOnOrOff              + "\n" ;
  
      sendstring=sendstring+   "!9!" +String(gSampleRate)               + "\n" ;
      sendstring=sendstring+   "!10!" +String(gSecondsPerFile)               + "\n" ;
 
      
  
       sendstring=sendstring+   "!11!"+ String(gFreeSpaceGB)                  + "\n" ;
       sendstring=sendstring+   "!12!" +gMicType                  + "\n" ;
  
       sendstring=sendstring+   "!13!" +gMicBitShift                  + "\n" ;
       sendstring=sendstring+   "!14!" +gLocationCode                + "\n" ;
      sendstring=sendstring+   "!15!" +gLocationAccuracy                + " m\n" ;

     
      sendstring=sendstring+   "!16!"+gSessionIdentifier                         + "\n" ;
     
      
                      
                           
      

      btwrite(sendstring);
      



  



} 










void updateFirmware() {
  
  Serial.println("IN UpdateFirmware");
  
   //SPIFFS.remove("/update.txt");


   //startSD();



    Serial.println("before fileopen");
    //delay(1000);    
    File file = SD.open("/eloc/update/update.bin");
    Serial.println("after fileopen ");
    //delay(1000);    
    
  
    size_t fileSize = file.size();
 
    
    if(!Update.begin(fileSize)){
       
       Serial.println("Cannot do the update");
       delay(2000);
       //file.close();
       //SD.remove("/eloc/update/update.bin");
       LEDflashError();
       ESP.restart();
       //return;
      
    };
    
    //SPIFFS.remove("/update.txt");
    Update.writeStream(file);
 
    if(Update.end()){
       
      Serial.println("Successful update");
      //delay(2000);  
      //btwrite("Successful update"); 
       //ESP.restart();
    }else {
       
      Serial.println("Error Occurred: " + String(Update.getError()));
       delay(1000);
       //btwrite("Error Occurred: " + String(Update.getError())); 
      //file.close();
      //delay(1000);
      //startSD();
      //delay(1000);
      //Serial.println("deleting update.bin");
      //SD.remove("/eloc/update/update.bin");
       //SD.remove("/eloc/update/update.bin");
      LEDflashError();
      ESP.restart();
     
    }
     
  
    Serial.println("Reset in 1 seconds...");

    digitalWrite(STATUS_LED,HIGH);
    delay(3000);
    digitalWrite(STATUS_LED,LOW);
    ESP.restart();
  
}






String getSubstring(String data, char separator, int index)
{
    int found = 0;
    int strIndex[] = { 0, -1 };
    int maxIndex = data.length() - 1;

    for (int i = 0; i <= maxIndex && found <= index; i++) {
        if (data.charAt(i) == separator || i == maxIndex) {
            found++;
            strIndex[0] = strIndex[1] + 1;
            strIndex[1] = (i == maxIndex) ? i+1 : i;
        }
    }
    return found > index ? data.substring(strIndex[0], strIndex[1]) : "";
}




void writeMicInfo() {
  File file2 = SPIFFS.open("/micinfo.txt", FILE_WRITE);
  
  file2.print(gMicType+'\n');
  file2.print(gMicBitShift+'\n');
  file2.print(gMicGPSCoords+'\n');
  file2.print(gMicPointingDirectionDegrees+'\n');
  file2.print(gMicHeight+'\n');
  file2.print(gMicMountType+'\n');
  file2.print(gMicBluetoothOnOrOff+'\n');
  file2.close();
  Serial.println("micinfo: "+gMicType+"  "+gMicBitShift+"  "+gMicGPSCoords+"  "+gMicPointingDirectionDegrees+" "+gMicHeight+" "+gMicMountType+" "+gMicBluetoothOnOrOff);





}


void readMicInfo() {
     if(!(SPIFFS.exists("/micinfo.txt"))){

      Serial.println("micinfo.txt not exist");
      writeMicInfo();
      
        

    }
    File file2 = SPIFFS.open("/micinfo.txt", FILE_READ);
    gMicType=file2.readStringUntil('\n');
    gMicType.trim();
    gMicBitShift=file2.readStringUntil('\n');
    gMicBitShift.trim();
    gMicGPSCoords=file2.readStringUntil('\n');
    gMicGPSCoords.trim();
    gMicPointingDirectionDegrees=file2.readStringUntil('\n');
    gMicPointingDirectionDegrees.trim();
    gMicHeight=file2.readStringUntil('\n');
    gMicHeight.trim();
    gMicMountType=file2.readStringUntil('\n');
    gMicMountType.trim();
    gMicBluetoothOnOrOff=file2.readStringUntil('\n');
    gMicBluetoothOnOrOff.trim();

    file2.close();
    Serial.println("micinfo: "+gMicType+"  "+gMicBitShift+"  "+gMicGPSCoords+"  "+gMicPointingDirectionDegrees+" "+gMicHeight+" "+gMicMountType+" "+gMicBluetoothOnOrOff);
 

}




void sendSettings() {
  btwrite("#"+String(gSampleRate)+"#"+String(gSecondsPerFile)+"#"+gLocation); 
  vTaskDelay(pdMS_TO_TICKS(100));
  //btwrite("elocName: "+readNodeName() + " "+gFirmwareVersion);
  vTaskDelay(pdMS_TO_TICKS(100));
  //btwrite(String(gFreeSpace)+ " GB free");
  
}

void writeSettings(String settings) {
      
    settings.trim();


    if (settings.endsWith("getstats"))  {
        btwrite("\n\n");
        sendElocStatus();
        btwrite("\n\n");
        delay(500);
        sendSettings();
        return;
    }


 

    if (settings.endsWith("vcal"))  {
        
        btwrite("\n\nCalibrating voltage with VLow="+String(gvLow)+ " volts\n");
        calculateVoltageOffset();
         btwrite("voltage offset is now "+String(gVoltageOffset));
        
        File file = SPIFFS.open("/voltageoffset.txt", FILE_WRITE); 
        file.print(gVoltageOffset);
        file.close();
        gVoltageCalibrationDone=true;
        delay(5000);
        sendSettings();
        return;
    }


    if (settings.endsWith("bton"))  {
         gMicBluetoothOnOrOff="on";
        btwrite("\n\nbluetooth ON while recording. Use phone to stop record.\n\n");
        writeMicInfo();
        sendSettings();
        return;
    }

    if (settings.endsWith("btoff"))  {
        gMicBluetoothOnOrOff="off";
        btwrite("\n\nbluetooth OFF while recording. Use button to stop record.\n\n");
        
        writeMicInfo();
        sendSettings();
        return;
    }




  
    if (settings.endsWith("micinfo"))  {
    
        btwrite("****** micinfo: ******** \nTYPE: " +gMicType+"\nGAIN: "+gMicBitShift+"\nGPSCoords: "+gMicGPSCoords+"\nDIRECTION: "+gMicPointingDirectionDegrees+"\nHEIGHT: "+gMicHeight+"\nMOUNT: "+gMicMountType+"\nBluetooth when record: "+gMicBluetoothOnOrOff);
        btwrite("\n");
        sendSettings();
        return;
    }


   
    if (settings.endsWith("help")) {
 
       //btwrite("\n***commands***\nXXsetgain (11=forest, 14=Mahout)\nXXXXsettype (set mic type)\nXXXXsetname (set eloc bt name)\nupdate (reboot + upgrade firmware)\nbtoff BT off when record\nbton BT on when record\ndelete (don't use)\n\n");
       sendSettings();
       return;

    }

    
    if (settings.endsWith("settype")) {
       gMicType= settings.substring(settings.lastIndexOf('#')+1, settings.length()-7);
       gMicType.trim();
       if (gMicType.length()==0) gMicType="ns"; 
       writeMicInfo();
       btwrite("Mic Type is now "+gMicType);
       sendSettings();
       return;

    }




    if (settings.endsWith("setgain")) {
       gMicBitShift= settings.substring(settings.lastIndexOf('#')+1, settings.length()-7);
       gMicBitShift.trim();
       btwrite(gMicBitShift);
       if (gMicBitShift == "11" || gMicBitShift == "12" || gMicBitShift == "13" || gMicBitShift == "14" || gMicBitShift == "15" || gMicBitShift == "16" ) {
 
       } else {
        btwrite("Error, mic gain out of range. (11 to 16) ");
        gMicBitShift="11"; 

       }
       
       writeMicInfo();
       //int temp=gMicBitShift.toInt();
       btwrite("Mic gain is now "+gMicBitShift);
       sendSettings();
       return;

    }   
    
    
    if (settings.endsWith("update")) {
      //updateFirmware();
      File temp = SPIFFS.open("/update.txt","w");
      temp.close();
      
      btwrite("\nEloc will restart for firmware update. Please re-connect in 1 minute.\n");
      delay(1000);
      ESP.restart();
      return;
    }

    if (settings.endsWith("setname")) {
      String temp;
      temp= settings.substring(settings.lastIndexOf('#')+1, settings.length()-7);
      temp.trim();
      //temp=settings.lastIndexOf('#');
    
      File file = SPIFFS.open("/nodename.txt", FILE_WRITE); 
      file.print(temp);
    
      file.close();
      Serial.println("new name: "+temp);
      btwrite("new name "+temp+"\n\n--- Restarting ELOC ----");
      vTaskDelay(pdMS_TO_TICKS(100));
      sendSettings();
      //readSettings();
      vTaskDelay(pdMS_TO_TICKS(500));
      ESP.restart();
      return;
    }




    //   if (settings.endsWith("sync")) {
    
    //       // btwrite("syncnow");
    //       // btwrite("syncing with time.google.com");
    //       // vTaskDelay(pdMS_TO_TICKS(5200));
    //       sendSettings();
      
    //        return;
    // }





      
      if (settings.endsWith("delete")) {
    
          //SPIFFS.
          SPIFFS.remove("/settings.txt");
          SPIFFS.remove("/nodename.txt");
          SPIFFS.remove("/micinfo.txt");

          btwrite("spiffs settings removed");
          vTaskDelay(pdMS_TO_TICKS(100));
          sendSettings();
      
           return;
    }
      

      


      
      
      File file = SPIFFS.open("/settings.txt", FILE_WRITE); 
        
        if(!file){
            Serial.println("There was an error opening the file for writing");
            return;
        }
    
        /*if(file.print(settings)){
            Serial.println("File was written");;
        } else {
            Serial.println("File write failed");
        }*/

        file.print(settings);
    
        file.close();
}




void readSettings() {
  
  //SPIFFS.remove("/settings.txt");
  //vTaskDelay(pdMS_TO_TICKS(100));

  if (!(SPIFFS.exists("/settings.txt"))) {
    writeSettings("#settings#"+String(gSampleRate)+"#"+String(gSecondsPerFile)+"#"+gLocation);
    Serial.println("wrote settings to spiffs");
    vTaskDelay(pdMS_TO_TICKS(100));

  }

  
  File file2 = SPIFFS.open("/settings.txt");
 
    if(!file2){
 
      Serial.println("Failed to open file for reading");
      return;
        

    }
 


  //String temp = file2.readStringUntil('\n');

  String temp = file2.readString();
  temp.trim();


  gSampleRate=getSubstring(temp, '#', 2).toInt();
  //temp 
  //if (gSampleRate==44100) gSampleRate=48000;
  gSecondsPerFile=getSubstring(temp, '#', 3).toInt();
  gLocation= getSubstring(temp, '#', 4);
  gLocation.trim();

  Serial.println("settings read: "+temp);

    /*Serial.println("File Content:");
 
    while(file2.available()){
 
        Serial.write(file2.read());
    }*/

 
    file2.close();

}








void mountSDCard(){
  //return;
  Serial.println("Mounting SDCARD");
 
  #ifdef USE_SPI_VERSION
    theSDCardObject= new SDCard("/sdcard", PIN_NUM_MISO, PIN_NUM_MOSI, PIN_NUM_CLK, PIN_NUM_CS);
  #endif

  #ifdef USE_SDIO_VERSION
   theSDCardObject = new SDCardSDIO("/sdcard");
  #endif


  vTaskDelay(pdMS_TO_TICKS(100));
  
  if (!gMountedSDCard) {
    btwrite("please check sd card");
    LEDflashError();
    //delay(1000);
    //sendSettings();
  } else {
    if (!(folderExists("/sdcard/eloc"))) mkdir("/sdcard/eloc", 0777);
    
    //btwrite("sdcard ready"); 
  }


  
  vTaskDelay(pdMS_TO_TICKS(100));
  //btwrite("waiting for record button");

}




long getTimeFromTimeObjectMS() {
    return(timeObject.getEpoch()*1000L+timeObject.getMillis());

}









void wait_for_button_push()
{
 
  //digitalWrite(BATTERY_LED,LOW);
  //bool gBatteryLEDToggle=false;
  float currentvolts;
  currentvolts= getVoltage()+ gVoltageOffset;
  
  gRecording=false;
  //getVoltage();
  boolean sentElocStatus=false;
  int loopcounter=0;
  Serial.println( "waiting for button or bluetooth");
  Serial.println( "voltage is "+String(getVoltage()+gVoltageOffset));
  
  
  int64_t timein= getSystemTimeMS();


  int gotrecord=false;
  int leddelay;
  String serialIN;
 
  Serial.println(" two following are heap size, total and max alloc ");
  Serial.println(ESP.getFreeHeap());
  Serial.println(ESP.getMaxAllocHeap()); 
  //mountSDCard();
  //freeSpace();
 
  while (!gotrecord)
  {
      //getVoltage();
      //gotrecord=false;
      //Serial.println( "waiting for buttonpress");
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
        //Serial.println(serialIN);
          //if (serialIN.startsWith("settingsRequest")) {
          //   btwrite("#"+String(gSampleRate)+"#"+String(gSecondsPerFile)+"#"+gLocation); 
          //}
          
          
           if (serialIN.startsWith("record")) {
              btwrite("\n\nYou are using an old version of the Android app. Please upgrade\n\n");

           }
          
          if (serialIN.startsWith( "_setClk_")) {
                   Serial.println("setClk starting");
                  //string will look like _setClk_G__120____32456732728  //if google, 12 mins since last phone sync 
                  //                   or _setClk_P__0____43267832648  //if phone, 0 min since last phone sync
                  
                  
                  String everything = serialIN.substring(serialIN.indexOf("___")+3,serialIN.length());
                  everything.trim();
                  String seconds = everything.substring(0,10); //was 18
                  String milliseconds=everything.substring(10,everything.length()); 
                  Serial.println("timestamp in from android GMT "+everything    +"  sec: "+seconds + "   millisec: "+milliseconds);
                  String minutesSinceSync=serialIN.substring(11,serialIN.indexOf("___"));
                  gSyncPhoneOrGoogle=serialIN.substring(8,9);
                  //Serial.println(minutesSinceSync);
                // Serial.println("GorP: "+GorP);
                //delay(8000);
                // Serial.println(test);
                  //Serial.println(seconds);
                  //Serial.println(milliseconds);
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
                  
                  //Serial.println("atol(minutesSinceSync.c_str()) *60L*1000L "+String(atol(minutesSinceSync.c_str()) *60L*1000L));
                  gLastSystemTimeUpdate=getTimeFromTimeObjectMS() -(      atol(minutesSinceSync.c_str()) *60L*1000L);
                  timein= getSystemTimeMS();
                  //Serial.println("timestamp in from android GMT "+everything    +"  sec: "+seconds + "   millisec: "+milliseconds);
                  //ESP_LOGI("d", "new timestamp from new sys time (local time) %lld", time_us  ); //this is 7 hours too slow!
                  //ESP_LOGI("d","new timestamp from timeobJect (local time) %lld",gLastSystemTimeUpdate);
                  

                  
                  //btwrite("time: "+timeObject.getDateTime()+"\n");
                   if (!sentElocStatus) {
                      sentElocStatus=true;
                      sendElocStatus();
                   }
                   Serial.println("setClk ending");
          }          
    
    
          if (serialIN.startsWith( "setGPS")) {
               // read the location on startup? 
               //only report recorded location status? 
               // need to differentiate between manual set and record set.
              gLocationCode=  serialIN.substring(serialIN.indexOf("^")+1,serialIN.indexOf("#") );
              gLocationCode.trim();
              gLocationAccuracy= serialIN.substring(serialIN.indexOf("#")+1,serialIN.length() );
              gLocationAccuracy.trim();
              Serial.println("loc: "+gLocationCode+"   acc "+gLocationAccuracy);
              
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
     

         if (loopcounter==0) {
                currentvolts= getVoltage()+ gVoltageOffset;
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
      
      
      
    
  
  
  
  }
 
 //mountSDCard();
 gSentSettings=false; 

}






String createFilename(){
   // ESP_LOGI(TAG, "Wrote %d samples in %lld microseconds",   samples_read, end - start);
    //char *theFilename=itoa(timeObject.getDay());
    // timeObject.setTime(atol(timeString.c_str())+(TIMEZONE_OFFSET*60L*60L));
   // (timeObject.getDay().  );
  //std::string s = std::to_string(42);
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
  //Serial.println(timeObject.getTimeDate(true));
  //  Serial.println(timeObject.getTimeDate(false));
  //createDir(SD, "/sdcard/mydir");
  //fopen("/sdcard/eloc");
  //gLastSystemTimeUpdate=getSystemTimestampInMilliseconds();
  
  //char buf[50];
  
  //unsigned long testID = 1716526225;
  //ltoa(getTimeFromTimeObjectMS(), buf, 10);  //same prob

  long timeDiffMinutes= (getTimeFromTimeObjectMS() -gLastSystemTimeUpdate)/1000L/60L;
  String elapsedTimeCode = getTimeDifferenceCode(timeDiffMinutes);
  //String currentTimestamp = String(getSystemTimestampInMilliseconds());
  String currentTimestamp="^"+gSyncPhoneOrGoogle+elapsedTimeCode+uint64ToString(getSystemTimeMS());
  
  

String theFilename="/sdcard/eloc/"+gSessionIdentifier+"/"+gSessionIdentifier+"_"+year+"-"+month+"-"+day+"_"+hour+"-"+minute+"-"+second+".wav";
  
  //String theFilename = "/sdcard/eloc/"+readNodeName()+"_"+gLocation+"_"+year+"-"+month+"-"+day+"_"+hour+"-"+minute+"-"+second+"_MIC_"+gMicType+"_"+gMicBitShift +currentTimestamp+"_{"+gLocationCode+"}{"+gLocationAccuracy+"}"+ ".wav";
 
  Serial.println(theFilename);
  return theFilename;
}



void disableBluetooth() {
          SerialBT.end(); 
          
          
          esp_bluedroid_disable();
          esp_bluedroid_deinit();
         esp_bt_controller_disable();
         esp_bt_controller_deinit();
        Serial.println("serialbt ended");
         gBlueToothISDisabled=true;
       

}

void enableBluetooth() {
          //esp_bluedroid_enable();
          //esp_bluedroid_init();
         //esp_bt_controller_enable();
         //esp_bt_controller_init();
         //BluetoothSerial temp;
         //SerialBT.end();
         //vTaskDelay(pdMS_TO_TICKS(1000));
         
         SerialBT.begin(readNodeName()); //getting socket errors
         vTaskDelay(pdMS_TO_TICKS(500));
         Serial.println("serialbt readstring"+ SerialBT.readString());
        vTaskDelay(pdMS_TO_TICKS(500));
        gBlueToothISDisabled=false;


}














void record()
{
  
   Serial.println("in record");

  gRecording=true;
  boolean sentrecord=false;  
  vTaskDelay(pdMS_TO_TICKS(300));
  btwrite("recording");
  vTaskDelay(pdMS_TO_TICKS(300));
  // do the session stuff
  gSessionIdentifier=readNodeName()+"_"+uint64ToString(getSystemTimeMS());
  String foo = "/sdcard/eloc/"+gSessionIdentifier;
  const char *folder = foo.c_str();

  mkdir( folder, 0777);

  
  
  
  sendElocStatus();
  saveStatusToSD();


  if (gMicBluetoothOnOrOff=="off") {
      changeBootPartition(1); 
      //vTaskDelay(pdMS_TO_TICKS(1000));
     // esp_restart();  //reboots and starts recording in low power mode 
      delete theSDCardObject;
      disableBluetooth(); //didn't work.
      resetESP32();
  }
 
 





  bool deepSleep=false;
  sentrecord=true;
  //gsentre
    vTaskDelay(pdMS_TO_TICKS(300));
    Serial.println("after record start");
    if (gMicBluetoothOnOrOff.equalsIgnoreCase("off")) {
        gDisableBluetoothWhenRecording=true;
    } else {
      gDisableBluetoothWhenRecording=false;
    }
    if ( gDisableBluetoothWhenRecording==true) {
          btwrite("recording");
          //gSampleBlockSize=gSampleBlockSizeNoBluetooth;
          btwrite("");btwrite("");
          btwrite("ELOC will disconnect. USE button to stop recording.");
        
          btwrite("");btwrite("");
          vTaskDelay(pdMS_TO_TICKS(300));
          //disable bluetooth
          disableBluetooth();


    }  else {
         //gSampleBlockSize=gSampleBlockSizeBluetooth;  //must be lower because of big memory bluetooth stack
    }
  
  
  int64_t recordStartTime= esp_timer_get_time();
  
  float loops;
  int samples_read;
  int64_t longestWriteTimeMillis=0;
  int64_t writeTimeMillis=0;
  int64_t bufferUnderruns=0;
  float bufferTimeMillis=(((float)gBufferCount*(float)gBufferLen)/(float)gSampleRate)*1000;
  int64_t writestart,writeend,loopstart,looptime,temptime;
  digitalWrite(STATUS_LED,LOW);
  digitalWrite(BATTERY_LED,LOW);

  gbitShift=gMicBitShift.toInt();
  
  //Serial.println("\n\n\n\n\n\n\n\n\gBitShift "+String(gbitShift));
  

  
  I2SSampler *input;
 
  
  i2s_mic_Config.sample_rate=gSampleRate;
  //i2s_mic_Config.sample_rate=4000;
  input = new I2SMEMSSampler(I2S_NUM_0, i2s_mic_pins, i2s_mic_Config,false); //the true at the end is the timing fix
  
  
  //graw_samples = (int32_t *)malloc(sizeof(int32_t) * gSampleBlockSize);
  graw_samples = (int32_t *)malloc(sizeof(int32_t) * 1000);
  int16_t *samples = (int16_t *)malloc(sizeof(int16_t) *gSampleBlockSize);
 
  Serial.print("samplerate: ");  Serial.println(gSampleRate); 
  Serial.print("seconds per file: "); Serial.println(gSecondsPerFile); 
  
  FILE *fp;
  WAVFileWriter *writer;

  
  //ESP_LOGI(TAG, "Start recording");
 
  
  input->start();
  
  gGPIOButtonPressed=false;
  //btwrite("Start recording "+String(input->sample_rate()));
 
  
  boolean stopit = false;
  
 
  //i2s_set_clk(I2S_NUM_0, gSampleRate, I2S_BITS_PER_SAMPLE_32BIT, I2S_CHANNEL_MONO);
  gRealSampleRate=(int32_t)(i2s_get_clk(I2S_NUM_0));
  ESP_LOGI(TAG, "I2s REAL clockrate  %d", gRealSampleRate  );
    //32k=32894 
    //16k=16025
    //8k=8012
  //delay(200);

  while (!stopit) {
      
      

      fp = fopen(createFilename().c_str(), "wb");
      //writer = new WAVFileWriter(fp, input->sample_rate());
       writer = new WAVFileWriter(fp, gRealSampleRate);
      
      long loopCounter=0;
      int64_t recordupdate = esp_timer_get_time();
      //int64_t temptime;
    

 
 
 
      loops= (float)gSecondsPerFile/    ((gSampleBlockSize)/((float)gSampleRate)) ;
      Serial.print("loops: "); Serial.println(loops); 
      loopstart=esp_timer_get_time(); 
      while (loopCounter < loops ) {
            loopCounter++;
             temptime=esp_timer_get_time();
            looptime=(temptime-loopstart);
            loopstart=temptime;


            //lastlooptime, thislooptime, maxlooptime,
            
            //loopthistime = esp_timer_get_time();
            //testonly**********************************
            //if (loopCounter==12) gVoltage=2.2;
            //************************end test
            samples_read = input->read(samples, gSampleBlockSize);
            //setCpuFrequencyMhz(240);
            writestart = esp_timer_get_time();
            writer->write(samples, samples_read);
            writeend = esp_timer_get_time();
            //setCpuFrequencyMhz(80);
            //digitalWrite(STATUS_LED,LOW);
            writeTimeMillis=(writeend - writestart)/1000;
            if (writeTimeMillis > longestWriteTimeMillis) longestWriteTimeMillis=writeTimeMillis;
            if (writeTimeMillis>bufferTimeMillis) bufferUnderruns++;
            ESP_LOGI(TAG, "Wrote %d samples in %lld ms. Longest: %lld. buffer (ms): %f underrun: %lld loop:%lld",   samples_read,writeTimeMillis,longestWriteTimeMillis,bufferTimeMillis,bufferUnderruns,looptime/1000);
            

             //gFreeSpaceKB-=(gSampleBlockSize*2)/1000;
             gFreeSpaceGB-=(  ((float)gSampleBlockSize*2.0) /1000000000.0);
            if ((esp_timer_get_time() - recordupdate  ) > 900000 ) { //less than a second
               
 
                //Serial.println("freeSpaceGB "+String(gFreeSpaceGB));
                //uptimeUpdate();
                recordupdate=esp_timer_get_time(); 
                if (!gDisableBluetoothWhenRecording) {
                    if (!SerialBT.connected())  {
                      sentrecord=false;
                    } else {
                      //int64_t recordEndTime= esp_timer_get_time();
                      gSessionRecordTime=esp_timer_get_time()-recordStartTime;
                      //gTotalRecordTimeSinceReboot=gTotalRecordTimeSinceReboot+(esp_timer_get_time()-recordStartTime); 
                      //Serial.println("session rec time = "+uint64ToString(gSessionRecordTime/1000/1000)+" sec");
                      if (!sentrecord) {
                        btwrite("recording");
                        vTaskDelay(pdMS_TO_TICKS(200));
                        sendElocStatus();
                        sentrecord=true;
                      
                      }
                    }

                    if (SerialBT.available()) {if (SerialBT.readString().startsWith("stoprecord")) {stopit=true; loopCounter=10000000L;} }
                }
                if (gGPIOButtonPressed)  {stopit=true; loopCounter=10000000L; }
               // ok fix me put me back in if (gFreeSpaceGB<0.2f)  {stopit=true; loopCounter=10000000L; }
                
                 //voltage check
                if ((loopCounter % 10)==0 ) {
                    ESP_LOGI(TAG, "Voltage check...");
                   if ((getVoltage()+gVoltageOffset)<gvOff) {
                     stopit=true; loopCounter=10000000L;
                     ESP_LOGI(TAG, "Voltage LOW-OFF. Stopping record. " );
                     deepSleep=true;

                     
                   }   
                } 

               
    
              }
            
            
            
          
         
          
      

          
      }
    //digitalWrite(STATUS_LED,LOW);  
    writer->finish();
    fclose(fp);
    delete writer;
    

  }
 
  input->stop();
  free(samples);
  free(graw_samples);
  


  gSessionRecordTime=esp_timer_get_time()-recordStartTime;
  //int64_t recordEndTime= esp_timer_get_time();
  gTotalRecordTimeSinceReboot=gTotalRecordTimeSinceReboot+gSessionRecordTime; 
  Serial.println("total record time since boot = "+uint64ToString(gTotalRecordTimeSinceReboot/1000/1000)+" sec");
  if (gDisableBluetoothWhenRecording ) {
    enableBluetooth(); 
    //ESP.restart(); //mark
    //vTaskDelay(pdMS_TO_TICKS(3000));
  }

  ESP_LOGI(TAG, "Finished recording");
  //btwrite("Finished recording");
   gRecording=false;
   gSessionRecordTime=0;
    gSessionIdentifier="";
   if (deepSleep) doDeepSleep();
}





void main_task(void *param)
{
    Serial.println("\nBeginning Main Task\n");
  
 









 


   if(!SPIFFS.begin(true)){
        Serial.println("An Error has occurred while mounting SPIFFS");
        //return;
   }


// so it seems that if any file is opened before sdcard firmare update, it fails.
// also startsd must be outside here
if (SPIFFS.exists("/update.txt")) {

  gWillUpdate=true;
  SPIFFS.remove("/update.txt");
  SPIFFS.end();
  delay(200);
  //startSD();
  SD.begin();
  delay(500);
  

}


//old if (gWillUpdate) updateFirmware();
 if (gWillUpdate) {
   
   changeBootPartition(2);
   delete theSDCardObject;
   resetESP32();


 }

SPIFFS.begin();
delay(50);
//resetPeripherals(); does not help.
delay(50);
SerialBT.begin(readNodeName(),false);
delay(100);
if (SerialBT.isReady())  {
      Serial.println("SerialBT is ready ------------------------------------------------------ ");
} else {
       Serial.println("SerialBT is NOT ready --------------------------------------------------- ");
}


readSettings();
readMicInfo();
// read the voltage offset

String offset="0.0";
//SPIFFS.remove("/voltageoffset.txt");
if((SPIFFS.exists("/voltageoffset.txt"))){
    gVoltageCalibrationDone=true;
    File file = SPIFFS.open("/voltageoffset.txt", FILE_READ); 
    offset=file.readString();
    file.close();
    gVoltageOffset=offset.toFloat();
}
//Serial.println(" \ngvoltage offset is  "+offset);



//marker
  print_reset_reason();  //RTC_NOINIT_ATTR 
  //delay(5000);
  //You can also test a system crash by having an assertion fail such as "assert(0);" which causes a SW_CPU_RESET 
  //graw_samples = (int32_t *)malloc(sizeof(int32_t) * 10000000000000l/0l);  
//two types of errors :  Exception - when the code attempts an illegal operation, like trying to write to non-existent memory location.

//Watchdog - if the code locks up, staying too long in a loop or processing any other task without any pauses, which would prevent vital processes like Wi-Fi communication from running.



  while (true)
  {
    
    
    
    
    // do the reset stuff here 
    gRecording=true;   //tese are noinit variables
    gResumeRecordOnReboot=true; //test
    if (gRecording && gResumeRecordOnReboot) {
      //record();
      //what about bluetooth?
    }

    
    wait_for_button_push();
    

    //record(input, "/sdcard/test.wav");
    
    record();
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}

void IRAM_ATTR butttonISR() {
    //Statements;
    //Serial.println(" two following are heap size, total and max alloc ");
    Serial.println(ESP.getFreeHeap());
    if (gRecording)  {
    
        gGPIOButtonPressed=true;
     
    } else {
      gGPIOButtonPressed=false;

    }
     
     //detachInterrupt(GPIO_BUTTON);

}





void setup()
{
 
  periph_module_reset(PERIPH_UART0_MODULE);
  periph_module_reset(PERIPH_UART1_MODULE);
  periph_module_reset(PERIPH_UART2_MODULE);
  Serial.begin(115200);
  
  Serial.println("\nSETUP--start\n");
  //const char *temp=VERSION;
  //Serial.println(strcat("VERSION: ", temp));
  ESP_LOGI(TAG, "VERSION: %s ", VERSION);
 
  #ifdef USE_SPI_VERSION
      pinMode(STATUS_LED,OUTPUT);
      pinMode(BATTERY_LED,OUTPUT);

      pinMode(GPIO_BUTTON, INPUT_PULLUP);
      pinMode(OTHER_GPIO_BUTTON, INPUT_PULLUP);
      pinMode(VOLTAGE_PIN,INPUT);
      analogSetPinAttenuation(VOLTAGE_PIN, ADC_11db); //set channel attenuation?
      //adc_calibration_init();
      pinMode(PIN_NUM_MOSI, INPUT_PULLUP);
      pinMode(PIN_NUM_CLK, INPUT_PULLUP);
      pinMode(PIN_NUM_MISO, INPUT_PULLUP);
      pinMode(PIN_NUM_CS, INPUT_PULLUP);
  #endif

  #ifdef USE_SDIO_VERSION
      //Serial.println("1\n");
      pinMode(STATUS_LED,OUTPUT);  //fails when gpio #6 
      //Serial.println("2\n");
      pinMode(BATTERY_LED,OUTPUT);
      //Serial.println("3\n");
      pinMode(GPIO_BUTTON, INPUT_PULLUP);
      //Serial.println("4\n");
      pinMode(OTHER_GPIO_BUTTON, INPUT_PULLUP);
      //Serial.println("5\n");
      pinMode(VOLTAGE_PIN,INPUT);
      //Serial.println("6\n");
      analogSetPinAttenuation(VOLTAGE_PIN, ADC_11db); //set channel attenuation?
      //adc_calibration_init();

  #endif

Serial.println("\nSETUP--After--IFDEF\n");
setCpuFrequencyMhz(80);
Serial.println("\nSETUP--After Setcpufreq.\n");
 
//updateFirmware();


   Serial.println("\nSetup--Before Main Task xcreate\n");
  xTaskCreate(main_task, "Main", 8096, NULL, 0, NULL);
   Serial.println("\nSetup--After main Task xcreate\n");
  //xTaskCreatePinnedToCore (TASK_1,	"TASK_1",	4096, (void *)1, 1, NULL, CORE1);

}

void loop()
{
  vTaskDelete(NULL);

  /*
  The loopTask is pinned to CPU1. This means that CPU1 is very busy doing your empty loop() over and over and over again, so Task2 (which is also pinned to CPU1) is starved of CPU time.
  There are two easy solutions. Tell the scheduler to make it idle, or just delete the task:
  CODE: SELECT ALL

  void loop() {
    vTaskDelay(portMAX_DELAY);
    //OR
    vTaskDelete(NULL);
  }
  */
}