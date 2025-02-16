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

#include "ESP32Time.h"
#include "time.h"
#include "esp_app_format.h"
#include "esp_ota_ops.h"
#include "esp_log.h"
#include <stdio.h>
#include <sys/time.h>

static const char* TAG = "ESP32Time";

/*!
    @brief  Constructor for ESP32Time
    @param  epochBuildDate
            Build date in UNIX Time epoch format (optional)
*/
ESP32Time::ESP32Time(uint64_t epochBuildDate/* = 0*/) {
    /*
     * Retrieving build date from esp app description returns only the local
     * Build date, but not the absolute UTC timestamp. It is prefereable to 
     * initialize ESP32Time with the UTC Unix timestamp, but this must be provided externally
    const esp_app_desc_t *app_desc = esp_ota_get_app_description();
    struct tm timeinfo;
    char str[50];
    snprintf(str, sizeof(str), "%s %s", app_desc->date, app_desc->time);
    strptime(str, "%b %d %Y %H:%M:%S", &timeinfo);
    build_time_unix = mktime(&timeinfo);    
    setTime(build_time_unix, 0);
    strftime(str, 50, "%A, %B %d %Y", &timeinfo);
    ESP_LOGI(TAG, "Set boot time to %s (unix time = %lld)", str, build_time_unix);
    */
    build_time_unix = epochBuildDate;
    ESP_LOGI(TAG, "Set boot time to unix time = %lld", build_time_unix);
    boot_time_unix = build_time_unix;
}

/**
 * @brief Set initial build time as boot time reference
 * @param  epochBuildDate
 *         Build date in UNIX Time epoch format (optional)
 * @param  tz_offset
 *         Timezone offset (ignored if 0)
 * @note If time is not set the getLocalTime() will stuck for 5 ms due to invalid timestamp
*/
void ESP32Time::initBuildTime(uint64_t epochBuildDate, int32_t tz_offset) {
    
    build_time_unix = epochBuildDate;
    ESP_LOGI(TAG, "Set boot time to unix time = %lld)", build_time_unix);
    boot_time_unix = build_time_unix;

    this->setTime(epochBuildDate, 0);
    this->setTimeZone(tz_offset);
    String time= getTimeDate(false);
    ESP_LOGI(TAG, "Setting initial time to Unix time: %ld & timezone UTC %+d --> Current Time %s", 
            getEpoch(), tz_offset, time.c_str());
}

/*!
    @brief  set the internal RTC time
    @param  sc
            second (0-59)
    @param  mn
            minute (0-59)
    @param  hr
            hour of day (0-23)
    @param  dy
            day of month (1-31)
    @param  mt
            month (1-12)
    @param  yr
            year ie 2021
    @param  ms
            microseconds (optional)
*/
void ESP32Time::setTime(int sc, int mn, int hr, int dy, int mt, int yr, int ms) {
  // seconds, minute, hour, day, month, year $ microseconds(optional)
  // ie setTime(20, 34, 8, 1, 4, 2021) = 8:34:20 1/4/2021
  struct tm t = {0, 0, 0, 0, 0, 0, 0, 0, 0};        // Initialize to all 0's
  t.tm_year = yr - 1900;    // This is year-1900, so 121 = 2021
  t.tm_mon = mt - 1;
  t.tm_mday = dy;
  t.tm_hour = hr;
  t.tm_min = mn;
  t.tm_sec = sc;
  time_t timeSinceEpoch = mktime(&t);
  setTime(timeSinceEpoch, ms);
}

/*!
 * @brief set time based a string
 * 
 * @param timeStr 
 *        timestamp formated as string
 * @param format 
 *        the format of the timeStr string (see strptime for details)
 */
void ESP32Time::setTime(const char* timeStr, const char* format) {
    struct tm tm;
    strptime(timeStr, format, &tm);
    time_t timeSinceEpoch = mktime(&tm);
    setTime(timeSinceEpoch, 0);
}

/*!
    @brief  set the internal RTC time
    @param  epoch
            epoch time in seconds
    @param  ms
            microseconds (optional)
*/
void ESP32Time::setTime(long epoch, int ms) {
    //BUGME: Why limit the max. time change since build to 10 years?! should be unlimited
  if ((epoch < build_time_unix) || (epoch > (build_time_unix + 60*60*24*365*10)))
    return;

  if (ms < 0 || ms > 999)
    return;

  auto old_time = getEpoch();
  if (old_time < build_time_unix) {
    // First time to set time
    // getEpoch() not accurate
    old_time = build_time_unix;
  }

  struct timeval tv;
  tv.tv_sec = epoch;  // epoch time (seconds)
  tv.tv_usec = ms;    // microseconds
  settimeofday(&tv, NULL);

  // Advance boot time by change in time
  boot_time_unix += (epoch - old_time);
}

int ESP32Time::setTimeZone(int32_t offset) {
  if (offset < -12 || offset > 14)
    return -1;

  String s;
  if (offset == 0) {
    s = "UTC";
  } else {
    // Reverse sign
    offset = - offset;
    s = "UTC" + String(offset);
  }
  setenv("TZ", s.c_str(), 1);
  tzset();
  return 0;
}

/*!
    @brief  get the internal RTC time as a tm struct
*/
tm ESP32Time::getTimeStruct() {
  struct tm timeinfo;
  getLocalTime(&timeinfo);
  return timeinfo;
}

/*!
    @brief  get the time and date as an Arduino String object
    @param  mode
            true = Long date format
            false = Short date format
*/
String ESP32Time::getDateTime(bool mode){
    struct tm timeinfo = getTimeStruct();
    char s[51];
    if (mode)
    {
        strftime(s, 50, "%A, %B %d %Y %H:%M:%S", &timeinfo);
    }
    else
    {
        strftime(s, 50, "%a, %b %d %Y %H:%M:%S", &timeinfo);
    }
    return String(s);
}

/*!
    @brief  get the time and date in format 2024-01-25_18_09_01
*/
String ESP32Time::getDateTimeFilename() {
    struct tm timeinfo = getTimeStruct();
    char s[51];
    strftime(s, 50, "%F_%H-%M-%S", &timeinfo);
    return String(s);
}

/*!
    @brief  get the time and date as an Arduino String object
    @param  mode
            true = Long date format
            false = Short date format, e.g.	16:01:12 Fri, Feb 02 2024
*/
String ESP32Time::getTimeDate(bool mode){
    struct tm timeinfo = getTimeStruct();
    char s[51];
    if (mode)
    {
        strftime(s, 50, "%H:%M:%S %A, %B %d %Y", &timeinfo);
    }
    else
    {
        strftime(s, 50, "%H:%M:%S %a, %b %d %Y", &timeinfo);
    }
    return String(s);
}

/*!
    @brief  get the local time as an Arduino String object
*/
String ESP32Time::getTime(){
    struct tm timeinfo = getTimeStruct();
    char s[51];
    strftime(s, 50, "%H:%M:%S", &timeinfo);
    return String(s);
}

/*!
    @brief  get the time as an Arduino String object with the specified format
    @param	format
            time format
            http://www.cplusplus.com/reference/ctime/strftime/
*/
String ESP32Time::getTime(String format){
    struct tm timeinfo = getTimeStruct();
    char s[128];
    char c[128];
    format.toCharArray(c, 127);
    strftime(s, 127, c, &timeinfo);
    return String(s);
}

/*!
    @brief  get the date as an Arduino String object
    @param  mode
            true = Long date format
            false = Short date format
*/
String ESP32Time::getDate(bool mode){
    struct tm timeinfo = getTimeStruct();
    char s[51];
    if (mode)
    {
        strftime(s, 50, "%A, %B %d %Y", &timeinfo);
    }
    else
    {
        strftime(s, 50, "%a, %b %d %Y", &timeinfo);
    }
    return String(s);
}

/*!
    @brief  get the current milliseconds as long
*/
long ESP32Time::getMillis(){
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_usec/1000;
}

/*!
    @brief  get the current microseconds as long
*/
long ESP32Time::getMicros() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_usec;
}

/*!
    @brief  get the current epoch seconds as long
*/
long ESP32Time::getEpoch() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec;
}

/*!
    @brief  get the current local (incl. TZ) epoch seconds as long
*/
long ESP32Time::getLocalEpoch() {
    struct tm timeinfo = getTimeStruct();
    time_t timeSinceEpoch = mktime(&timeinfo);

    struct tm gmTm  = *gmtime(&timeSinceEpoch);
    time_t gmTime = mktime(&gmTm);
    time_t tz_offset = timeSinceEpoch - gmTime;

    time_t localTimeSinceEpoch = timeSinceEpoch + tz_offset;

    struct timeval tv;
    gettimeofday(&tv, NULL);
    ESP_LOGI(TAG, "getLocalEpoch: %ld, gmTime: %ld, timeval: %ld", 
        timeSinceEpoch, gmTime, localTimeSinceEpoch);
    return localTimeSinceEpoch;
}

/*!
    @brief  get the current seconds as int
*/
int ESP32Time::getSecond() {
    struct tm timeinfo = getTimeStruct();
    return timeinfo.tm_sec;
}

/*!
    @brief  get the current minutes as int
*/
int ESP32Time::getMinute() {
    struct tm timeinfo = getTimeStruct();
    return timeinfo.tm_min;
}

/*!
    @brief  get the current hour as int
    @param	mode
            true = 24 hour mode (0-23)
            false = 12 hour mode (0-12)
*/
int ESP32Time::getHour(bool mode){
    struct tm timeinfo = getTimeStruct();
    if (mode)
    {
        return timeinfo.tm_hour;
    }
    else
    {
        int hour = timeinfo.tm_hour;
        if (hour > 12)
        {
            return timeinfo.tm_hour-12;
        }
        else
        {
            return timeinfo.tm_hour;
        }

    }
}

/*!
    @brief  return current hour am or pm
    @param	lowercase
            true = lowercase
            false = uppercase
*/
String ESP32Time::getAmPm(bool lowercase){
    struct tm timeinfo = getTimeStruct();
    if (timeinfo.tm_hour >= 12)
    {
        if (lowercase)
        {
            return "pm";
        }
        else
        {
            return "PM";
        }
    }
    else
    {
        if (lowercase)
        {
            return "am";
        }
        else
        {
            return "AM";
        }
    }
}

int64_t ESP32Time::getSystemTimeMS() {
    // https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/system_time.html#get-current-time-in-milliseconds
    struct timeval tv_now;
    gettimeofday(&tv_now, NULL);
    int64_t time_us = ((int64_t)tv_now.tv_sec * 1000000L) + (int64_t)tv_now.tv_usec;
    time_us = time_us / 1000;
    return (time_us);
}

String ESP32Time::getSystemTimeMS_string() {
    // return(uint64ToString(getSystemTimeMS()));
    return(String(getSystemTimeMS()));
}

int64_t ESP32Time::getSystemTimeSecs() {
    struct timeval tv_now;
    gettimeofday(&tv_now, NULL);
    return (tv_now.tv_sec);
}

/*!
    @brief  get the current day as int (1-31)
*/
int ESP32Time::getDay(){
    struct tm timeinfo = getTimeStruct();
    return timeinfo.tm_mday;
}

/*!
    @brief  get the current day of week as int (0-6)
*/
int ESP32Time::getDayofWeek(){
    struct tm timeinfo = getTimeStruct();
    return timeinfo.tm_wday;
}

/*!
    @brief  get the current day of year as int (0-365)
*/
int ESP32Time::getDayofYear(){
    struct tm timeinfo = getTimeStruct();
    return timeinfo.tm_yday;
}

/*!
    @brief  get the current month as int (1-12)
*/
int ESP32Time::getMonth(){
    struct tm timeinfo = getTimeStruct();
    return (timeinfo.tm_mon)+1;
}

/*!
    @brief  get the current year as int
*/
int ESP32Time::getYear(){
    struct tm timeinfo = getTimeStruct();
    return timeinfo.tm_year+1900;
}

/*!
    @brief  get the current uptime as uint64_t
    @warning  Untested
*/
uint64_t ESP32Time::getUpTimeSecs() {
    return (getEpoch() - boot_time_unix);
}
