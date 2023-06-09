/*
 * Created on Wed Apr 26 2023
 *
 * Project: International Elephant Project (Wildlife Conservation International)
 *
 * The MIT License (MIT)
 * Copyright (c) 2023 Fabian Lindner
 * based on the work of @tbgilson (https://github.com/tbgilson)
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


#include <FS.h>
#include "SPIFFS.h"

//BUGME: encapsulate these in a struct & implement a getter
String gMicType="ns";
String gMicBitShift="11";
String gMicGPSCoords="ns";
String gMicPointingDirectionDegrees="ns";
String gMicHeight="ns";
String gMicMountType="ns";
String gMicBluetoothOnOrOff="on";



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

      printf("micinfo.txt not exist");
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