/*
   MIT License

  Copyright (c) 2021 Felix Biego

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.
*/

#ifndef ESP32TIME_H
#define ESP32TIME_H

#include <Arduino.h>

//TODO: create global instance of ESP32Time in .cpp file and extern declaration in .h like Arduino style
class ESP32Time {
    private:
        uint64_t boot_time_unix = 0;  // Some sort of reasonable default
        uint64_t build_time_unix = 0;  // Some sort of reasonable default
    public:
        ESP32Time(uint64_t epochBuildDate = 0);
        void setTime(long epoch = 1609459200, int ms = 0);  // default (1609459200) = 1st Jan 2021
        int setTimeZone(int32_t offset);
        void setTime(int sc, int mn, int hr, int dy, int mt, int yr, int ms = 0);
        void setTime(const char* timeStr, const char* format);
        void initBuildTime(uint64_t epochBuildDate, int32_t tz_offset);
        tm getTimeStruct();
        String getTime(String format);

        String getTime();
        String getDateTime(bool mode = false);
        String getDateTimeFilename();
        String getTimeDate(bool mode = false);
        String getDate(bool mode = false);
        String getAmPm(bool lowercase = false);
        int64_t getSystemTimeMS();
        String getSystemTimeMS_string();
        int64_t getSystemTimeSecs();
        int64_t getBuildTimeSecs() const {
            return build_time_unix;
        };

        long getEpoch();
        long getMillis();
        long getMicros();
        int getSecond();
        int getMinute();
        int getHour(bool mode = false);
        int getDay();
        int getDayofWeek();
        int getDayofYear();
        int getMonth();
        int getYear();
        uint64_t getUpTimeSecs();
};

#endif
