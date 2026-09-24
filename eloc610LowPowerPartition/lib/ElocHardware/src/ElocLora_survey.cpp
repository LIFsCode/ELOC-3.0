/*
 * LoRa coverage survey ("signal strength mapper").
 *
 * Project: International Elephant Project (Wildlife Conservation International)
 * The MIT License (MIT)
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
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
 */

/**
 * @file ElocLora_survey.cpp
 * @brief Walk or drive a route and map where the gateway can hear this device.
 *
 * Why it is shaped this way (the physics, in short):
 *
 *  - A LoRaWAN Class A device cannot listen for a signal. Its radio is asleep except in the two
 *    RX windows that open after its own uplink, and gateways do not beacon. "Check the signal"
 *    therefore always means "transmit and see what comes back".
 *
 *  - So the survey is UPLINK-FIRST. Position uplinks go out continuously and cost no downlink at
 *    all; TTN records the gateway-side RSSI/SNR for each one and the web map is built from that.
 *    A downlink (LinkCheckReq -> LinkCheckAns, giving the demodulation margin AT THE GATEWAY plus
 *    the number of gateways that heard us) is only requested periodically or on demand, because
 *    downlinks are the scarce resource: TTN's fair-use policy allows 10 per device per day.
 *
 *  - An uplink nobody hears leaves no trace on the server. That is why EVERY transmission is also
 *    written to the SD card with its frame counter: joining the CSV against Firestore on fCnt
 *    afterwards turns an ABSENT server record into a confirmed dead spot with a position on it,
 *    which is the most valuable half of the dataset and does not exist anywhere else.
 *
 *  - Sampling triggers on DISTANCE with a time floor, not on a fixed interval. A fixed 30 s gives
 *    36 m spacing on foot and 168 m from a vehicle on a bad road; distance decouples the two, and
 *    a device standing still costs nothing at all.
 *
 * Trigger summary:
 *   automatic  - moved >= minDistanceM and the per-SF time floor has elapsed. No downlink.
 *   GPIO0      - one immediate uplink, no downlink. Marks this spot on the map, costs nothing scarce.
 *   app button - one immediate uplink WITH a LinkCheckReq, so the margin comes back and is beeped out.
 *   periodic   - every linkCheckEveryN-th sample also carries a LinkCheckReq, which doubles as the
 *                feedback that drives the adaptive spreading-factor ladder.
 */

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include <time.h>

#include "ElocLora.hpp"
#include "ElocConfig.hpp"
#include "ElocStatus.hpp"
#include "Battery.hpp"
#include "SDCardSDIO.h"
#include "EasyBuzzer.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern const char* TAG;
extern ESP32Time timeObject;
extern SDCardSDIO sd_card;

/// Directory the session files live in. One CSV per session, named after the node and start time.
static const char* C_SURVEY_DIR = "/sdcard/survey";

/// Idle-time backstop: even if the device has not moved, log one point this often so a stationary
/// stretch is distinguishable from a gap in the log. Deliberately long - standing still should be
/// close to free, but not literally invisible.
static const uint32_t C_SURVEY_IDLE_INTERVAL_S = 600;

/// LinkCheckAns margin (dB above the gateway's demodulation floor) at or above which the link is
/// considered healthy enough to step the spreading factor back down towards SF7.
static const uint8_t C_SURVEY_HEALTHY_MARGIN_DB = 10;

/// Consecutive unanswered / healthy link checks before the ladder moves. Two is enough to shrug off
/// a single unlucky downlink without being slow to react.
static const uint8_t C_SURVEY_SF_STEP_THRESHOLD = 2;

/// The ladder itself. Skips SF8 and SF11 - each step is 2.5 dB and the intermediate rungs cost
/// airtime without telling you much the neighbouring ones do not.
static const uint8_t C_SURVEY_SF_LADDER[] = {7, 9, 10, 12};

// Duty-cycle floor per SF, seconds. The 1% AS923 limit for a 25-byte PHY payload needs 6.2 s at
// SF7 rising to 165 s at SF12; these carry ~20-60% headroom on top. sendReceive() also blocks
// ~6-7 s through RX1/RX2 regardless, which is why SF7 lands at 10 s rather than 7.
const uint32_t ElocLora::C_SF_MIN_INTERVAL_S[6] = {
    10,   // SF7   airtime  62 ms, 1% floor   6.2 s
    13,   // SF8   airtime 103 ms, 1% floor  10.4 s
    25,   // SF9   airtime 206 ms, 1% floor  20.6 s
    45,   // SF10  airtime 412 ms, 1% floor  41.2 s
    90,   // SF11  airtime 823 ms, 1% floor  82.3 s
    180,  // SF12  airtime 1646 ms, 1% floor 165 s
};

/*****************************************************************************************
 * Helpers
 *****************************************************************************************/

double ElocLora::surveyDistanceM(double lat1, double lng1, double lat2, double lng2) {
    static const double C_DEG_TO_M = 111320.0;   // metres per degree of latitude
    static const double C_DEG_TO_RAD = 0.017453292519943295;
    const double dLat = (lat2 - lat1) * C_DEG_TO_M;
    const double dLng = (lng2 - lng1) * C_DEG_TO_M * cos(((lat1 + lat2) * 0.5) * C_DEG_TO_RAD);
    return sqrt(dLat * dLat + dLng * dLng);
}

int ElocLora::surveyLevelFromMargin(int margin) {
    if (margin < 0)  return 0;   // no LinkCheckAns came back at all
    if (margin < 5)  return 1;   // marginal - packets will drop
    if (margin < 10) return 2;   // weak
    if (margin < 15) return 3;   // usable
    if (margin < 20) return 4;   // good
    return 5;                    // excellent
}

uint32_t ElocLora::surveyMinIntervalS() const {
    const surveyConfig_t& cfg = getSurveyConfig();
    uint32_t sfFloor = C_SF_MIN_INTERVAL_S[0];
    if ((mSurveySF >= 7) && (mSurveySF <= 12)) {
        sfFloor = C_SF_MIN_INTERVAL_S[mSurveySF - 7];
    }
    // The configured floor is a user preference; the per-SF floor is a duty-cycle limit. The
    // limit always wins, so raising the SF automatically slows the cadence without the app
    // having to know anything about airtime.
    return (cfg.minIntervalS > sfFloor) ? cfg.minIntervalS : sfFloor;
}

void ElocLora::surveyRefreshBudgetDay() {
    // Local day, because getLocalEpoch() is the clock this device actually keeps.
    const int32_t today = static_cast<int32_t>(timeObject.getLocalEpoch() / 86400);
    if (today != mSurveyBudgetDay) {
        mSurveyBudgetDay = today;
        mSurveyUplinkCnt = 0;
        mSurveyDownlinkCnt = 0;
        ESP_LOGI(TAG, "[survey] new day, airtime budgets reset");
    }
}

/*****************************************************************************************
 * Session lifecycle
 *****************************************************************************************/

void ElocLora::surveyApplyRadioSettings() {
    // ADR off, always. With it enabled the network moves the device between data rates mid-survey
    // and the margins stop being comparable - and worse, after ADR_ACK_LIMIT (64) uplinks with no
    // downlink the stack sets ADRACKReq and starts forcing its own data-rate fallback, which in an
    // uplink-only survey fires roughly half an hour in and fights the ladder below.
    node.setADR(false);

    // Belt and braces: make RadioLib itself refuse an early transmit. Nothing in this firmware has
    // ever called setDutyCycle(), so dutyCycleEnabled defaults to false and the stack would happily
    // transmit as fast as it is asked to. 36000 ms/hour is the 1% AS923 allowance.
    node.setDutyCycle(true, 36000);

    // Datarate is the inverse of SF in every region this device uses: DR5 = SF7 ... DR0 = SF12.
    const uint8_t dr = static_cast<uint8_t>(12 - mSurveySF);
    int16_t state = node.setDatarate(dr);
    if (state != RADIOLIB_ERR_NONE) {
        ESP_LOGW(TAG, "[survey] setDatarate(DR%u / SF%u) failed: %d", dr, mSurveySF, state);
    } else {
        ESP_LOGI(TAG, "[survey] radio set to SF%u (DR%u), min interval %u s",
                 mSurveySF, dr, surveyMinIntervalS());
    }
}

bool ElocLora::surveyBeginSession() {
    if (!sd_card.isMounted()) {
        ESP_LOGE(TAG, "[survey] cannot start: no SD card. The CSV is not optional - without it a "
                      "spot with no coverage leaves no trace anywhere.");
        return false;
    }
    // Hold the card against a hot-swap unmount on the status task, exactly as the row writer does.
    if (!sd_card.claimFs()) {
        ESP_LOGE(TAG, "[survey] cannot start: SD busy");
        return false;
    }

    if (mkdir(C_SURVEY_DIR, 0777) != 0 && errno != EEXIST) {
        ESP_LOGE(TAG, "[survey] cannot create %s (errno %d)", C_SURVEY_DIR, errno);
        sd_card.releaseFs();
        return false;
    }

    // yyyy-mm-dd hh:mm:ss -> yyyymmdd_hhmmss, so the name is safe on FAT and sorts chronologically.
    String ts = timeObject.getTime("%Y%m%d_%H%M%S");
    mSurveyCsvPath = String(C_SURVEY_DIR) + "/" + getDeviceInfo().nodeName + "_" + ts + ".csv";

    FILE* fp = fopen(mSurveyCsvPath.c_str(), "w");
    if (fp == nullptr) {
        ESP_LOGE(TAG, "[survey] cannot open %s", mSurveyCsvPath.c_str());
        mSurveyCsvPath = "";
        sd_card.releaseFs();
        return false;
    }
    // UTC, not device local time. Local time is a rendering, and its offset can change DURING a
    // session: the GPS derives a zone from longitude (which cannot know about DST, so it is an hour
    // out for half the year in most of Europe) while the app pushes its own with setTime, and the
    // two fight last-writer-wins. A survey that crosses one of those changes gets a column whose
    // meaning silently shifts partway down the file. UTC never moves, and it is the same clock as
    // TTN's received_at - so this column can finally be joined against the server records by time
    // and not only by frame counter.
    //
    // The trailing "clock" column says which source last set the wall clock, because UTC being
    // stable does not make it CORRECT: rows written before the first GPS sync or app setTime carry
    // the firmware build time and look perfectly plausible. "build" marks those as untrustworthy.
    fputs("utc,lat,lon,sats,kind,sf,fcnt,margin_db,gw_cnt,level,trigger,clock\n", fp);
    fclose(fp);
    sd_card.releaseFs();

    const surveyConfig_t& cfg = getSurveyConfig();
    mSurveySF           = static_cast<uint8_t>(cfg.startSF);
    mSurveyStartS       = timeObject.getLocalEpoch();
    mSurveyLastTxMs     = 0;
    mSurveyHasAnchor    = false;
    mSurveyHasPosition  = false;
    mSurveySampleCnt    = 0;
    mSurveyMissedChecks = 0;
    mSurveyGoodChecks   = 0;
    mSurveyLastMargin   = 0xFF;
    mSurveyLastGwCnt    = 0;
    surveyRefreshBudgetDay();
    surveyApplyRadioSettings();

    ESP_LOGW(TAG, "[survey] session started -> %s (SF%u, >=%u m, floor %u s, link check every %u)",
             mSurveyCsvPath.c_str(), mSurveySF, cfg.minDistanceM, surveyMinIntervalS(),
             cfg.linkCheckEveryN);
    return true;
}

void ElocLora::surveyEndSession(const char* reason) {
    ESP_LOGW(TAG, "[survey] session ended (%s): %u samples, %u uplinks / %u downlinks used today",
             reason ? reason : "?", mSurveySampleCnt, mSurveyUplinkCnt, mSurveyDownlinkCnt);

    // Write the KML/GPX now, so pulling the card straight out of a finished survey gives files
    // that open in Locus Map or Avenza with no further step. setSurveyExport re-runs it if this
    // fails, or if a session ended by a flat battery never got here at all.
    if (mSurveySampleCnt > 0) {
        surveyExport(nullptr);
    }

    mSurveyActive = false;
    mSurveyForceUplink = false;
    mSurveyForceCheck = false;

    // Hand the radio back to normal operation. ADR is left off deliberately: the heartbeat is a
    // once-a-day uplink and this firmware has never relied on ADR to pick its data rate.
    node.setDutyCycle(false);
}

esp_err_t ElocLora::surveyStart() {
    if (!mInitDone) {
        ESP_LOGE(TAG, "[survey] cannot start: LoRa is not joined");
        return ESP_ERR_INVALID_STATE;
    }
    if (mSurveyActive) {
        return ESP_OK;
    }
    if (!surveyBeginSession()) {
        mSurveyStartFailed = true;
        return ESP_FAIL;
    }
    mSurveyActive = true;
    mSurveyStartFailed = false;
    // Send the anchor sample straight away, with a link check, so the ranger gets an immediate
    // reading and the distance trigger has something to measure from.
    mSurveyForceCheck = true;
    return ESP_OK;
}

void ElocLora::surveyStop(const char* reason) {
    if (mSurveyActive) {
        surveyEndSession(reason ? reason : "stopped");
    }
    mSurveyStartFailed = false;
}

/*****************************************************************************************
 * Logging
 *****************************************************************************************/

void ElocLora::surveyWriteCsvRow(const char* kind, const char* trigger, bool hasFix,
                                 double lat, double lng, uint32_t fCnt, bool answered) {
    portENTER_CRITICAL(&mGpsInfoMux);
    const uint32_t sats = mGpsInfo.sats;
    portEXIT_CRITICAL(&mGpsInfoMux);

    if (mSurveyCsvPath.length() == 0) {
        return;
    }
    // The status task can unmount the card underneath us on a hot-swap.
    if (!sd_card.claimFs()) {
        ESP_LOGW(TAG, "[survey] SD busy, dropping a row");
        return;
    }
    if (!sd_card.isMounted()) {
        sd_card.releaseFs();
        return;
    }

    FILE* fp = fopen(mSurveyCsvPath.c_str(), "a");
    if (fp == nullptr) {
        ESP_LOGE(TAG, "[survey] cannot append to %s", mSurveyCsvPath.c_str());
        sd_card.releaseFs();
        return;
    }

    // One comma, not two: this single %s sits between the utc and sats fields and has to expand
    // to exactly the two columns lat,lon - so "no fix" is a single separator, not a pair.
    char pos[48] = ",";
    if (hasFix) {
        snprintf(pos, sizeof(pos), "%.6f,%.6f", lat, lng);
    }

    // ISO-8601 UTC. getEpoch() is gettimeofday() seconds with no offset applied, unlike getTime(),
    // which renders through getLocalTime() and therefore changes meaning whenever the zone does.
    // The app's setTime carries an absolute epoch in "seconds" and the timezone separately, so the
    // UTC written here is right even on a phone-set clock - only the presentation was ever wrong.
    char utc[24] = "";
    const time_t nowS = static_cast<time_t>(timeObject.getEpoch());
    struct tm gm;
    gmtime_r(&nowS, &gm);
    strftime(utc, sizeof(utc), "%Y-%m-%dT%H:%M:%SZ", &gm);

    // Which source last set the wall clock. Persisted in RTC, so it survives the duty-cycle wakes
    // that a long survey sleeps through. "build" means nothing has set it this deployment and the
    // timestamp on this row is firmware build time - plausible-looking and wrong.
    const char* clockSrc = "build";
    if (rtc_duty_cycle.magic == DUTY_CYCLE_RTC_MAGIC) {
        switch (rtc_duty_cycle.clockSource) {
            case CLOCK_SRC_GPS: clockSrc = "gps"; break;
            case CLOCK_SRC_APP: clockSrc = "app"; break;
            default:            clockSrc = "build"; break;
        }
    }

    // A check with no answer writes empty margin/gwCnt/level fields on purpose: "we asked and
    // heard nothing" has to be distinguishable from "we never asked", both here and on the map.
    if ((strcmp(kind, "check") == 0) && answered) {
        fprintf(fp, "%s,%s,%u,%s,%u,%u,%u,%u,%d,%s,%s\n",
                utc, pos,
                static_cast<unsigned>(sats),
                kind, mSurveySF, fCnt,
                mSurveyLastMargin, mSurveyLastGwCnt,
                surveyLevelFromMargin(static_cast<int>(mSurveyLastMargin)), trigger, clockSrc);
    } else {
        fprintf(fp, "%s,%s,%u,%s,%u,%u,,,%s,%s,%s\n",
                utc, pos,
                static_cast<unsigned>(sats),
                kind, mSurveySF, fCnt,
                (strcmp(kind, "check") == 0) ? "0" : "", trigger, clockSrc);
    }

    fclose(fp);
    sd_card.releaseFs();
}

void ElocLora::surveyPlayReadout(int level, uint8_t gwCnt) {
    if (!getSurveyConfig().audio) {
        return;
    }
    // Count the beeps: level N = N beeps, with the pitch rising too, so the reading is coded twice
    // over. People count reliably and judge absolute pitch badly, and a count needs no reference
    // tone. "No link" is one long low tone, which cannot be mistaken for any count.
    //
    // The frequencies avoid the tones already in use elsewhere: 98 Hz battery low, 261 Hz Bluetooth
    // on, 523 Hz idle, 523/659/784 LoRa join OK, 400/300 join failed.
    static const struct { unsigned freq; unsigned onMs; unsigned beeps; } C_READOUT[6] = {
        {  175, 400, 1 },   // 0 - no answer
        {  440,  70, 1 },   // 1 - marginal
        {  587,  70, 2 },   // 2 - weak
        {  740,  70, 3 },   // 3 - usable
        {  880,  70, 4 },   // 4 - good
        { 1175,  70, 5 },   // 5 - excellent
    };
    if ((level < 0) || (level > 5)) {
        return;
    }
    for (unsigned i = 0; i < C_READOUT[level].beeps; i++) {
        EasyBuzzer.singleBeep(C_READOUT[level].freq, C_READOUT[level].onMs);
        vTaskDelay(pdMS_TO_TICKS(C_READOUT[level].onMs + 70));
    }
    // Redundancy tick: more than one gateway heard us, which is a materially better site than one
    // and is not conveyed by the margin at all.
    if (gwCnt >= 2) {
        vTaskDelay(pdMS_TO_TICKS(60));
        EasyBuzzer.singleBeep(1568, 45);
        vTaskDelay(pdMS_TO_TICKS(80));
    }
    EasyBuzzer.stopBeep();
}

/*****************************************************************************************
 * Transmission
 *****************************************************************************************/

bool ElocLora::surveySend(bool wantLinkCheck, const char* trigger) {
    const surveyConfig_t& cfg = getSurveyConfig();

    surveyRefreshBudgetDay();
    if (mSurveyUplinkCnt >= cfg.maxUplinksPerDay) {
        ESP_LOGW(TAG, "[survey] daily uplink budget (%u) spent - stopping", cfg.maxUplinksPerDay);
        surveyEndSession("uplink budget");
        return false;
    }
    if (wantLinkCheck && (mSurveyDownlinkCnt >= cfg.maxDownlinksPerDay)) {
        // Do not abandon the session: the uplinks are still building the map, and they are the
        // part that is fully within fair use. Only the downlink is refused.
        ESP_LOGW(TAG, "[survey] daily downlink budget (%u) spent - sending without a link check",
                 cfg.maxDownlinksPerDay);
        wantLinkCheck = false;
    }

    portENTER_CRITICAL(&mGpsInfoMux);
    GpsInfo_t gpsInfo = mGpsInfo;
    portEXIT_CRITICAL(&mGpsInfoMux);

    const bool hasFix = gpsInfo.hasFix;
    int32_t latE5 = 0;
    int32_t lngE5 = 0;
    if (hasFix) {
        latE5 = static_cast<int32_t>(gpsInfo.lat * 100000.0);
        lngE5 = static_cast<int32_t>(gpsInfo.lng * 100000.0);
    }

    // Payload mirrors the intruder message layout, so the TTN formatter change is mechanical.
    // The margin/gwCnt carried here are from the PREVIOUS link check - this uplink cannot know
    // its own result yet. The authoritative number for the map is the gateway-side RSSI/SNR that
    // TTN attaches to this uplink anyway.
    uint8_t uplinkPayload[LORA_MAX_TX_PAYLOAD];
    uint8_t idx = 0;
    uplinkPayload[idx++] = (t_LoraMsgType::SURVEY_MSG << 4) | (LORA_MSG_VERS & 0x0F);
    for (int i = 3; i >= 0; i--) uplinkPayload[idx++] = (latE5 >> (i * 8)) & 0xFF;
    for (int i = 3; i >= 0; i--) uplinkPayload[idx++] = (lngE5 >> (i * 8)) & 0xFF;
    uplinkPayload[idx++] = mSurveyLastMargin;
    uplinkPayload[idx++] = static_cast<uint8_t>((mSurveyLastGwCnt & 0x0F) |
                                                (((mSurveySF - 6) & 0x0F) << 4));
    const uint8_t soc = static_cast<uint8_t>(Battery::GetInstance().getSoC());
    uplinkPayload[idx++] = static_cast<uint8_t>((hasFix ? 0x01 : 0x00) |
                                                (wantLinkCheck ? 0x02 : 0x00) |
                                                ((soc / 4) << 2));

    // A freshly joined session cannot answer a link check on its very first uplink (see
    // mFreshSession). Spend that uplink on the position alone and carry the check over to the next
    // one, which is a second or two later for a forced check and costs no extra airtime - rather
    // than reporting "no link" while sitting next to a gateway.
    if (wantLinkCheck && mFreshSession) {
        ESP_LOGI(TAG, "[survey] first uplink of a new session - deferring the link check");
        wantLinkCheck = false;
        mSurveyForceCheck = true;
    }

    if (wantLinkCheck) {
        int16_t reqState = node.sendMacCommandReq(RADIOLIB_LORAWAN_MAC_LINK_CHECK);
        if (reqState != RADIOLIB_ERR_NONE) {
            ESP_LOGW(TAG, "[survey] sendMacCommandReq(LINK_CHECK) failed: %d", reqState);
            wantLinkCheck = false;
        }
    }

    LoRaWANEvent_t uplinkDetails = {};
    int16_t state = sendReceiveWithRecovery(uplinkPayload, idx, &uplinkDetails);
    if (state < RADIOLIB_ERR_NONE) {
        // A transmit that never left is not a data point - do not log it as a dead spot, or the
        // map would blame the forest for a radio problem.
        ESP_LOGE(TAG, "[survey] uplink failed: %d", state);
        mSurveyLastTxMs = esp_timer_get_time() / 1000;
        return false;
    }

    // The session has now carried an uplink, so link checks work from here on.
    mFreshSession = false;
    mSurveyUplinkCnt++;
    mSurveySampleCnt++;
    mSurveyLastTxMs = esp_timer_get_time() / 1000;
    // "A sample has been sent" and "we have a position to measure distance from" are different
    // facts. Conflating them meant a session that never got a fix - indoors, or under canopy
    // before the first fix - never set the anchor, so the due-check below fired on every pass and
    // the device transmitted at the interval floor indefinitely, emptying the daily budget in
    // about an hour on nothing but null-position points.
    mSurveyHasAnchor = true;
    if (hasFix) {
        mSurveyLastLat = gpsInfo.lat;
        mSurveyLastLng = gpsInfo.lng;
        mSurveyHasPosition = true;
    }

    bool answered = false;
    if (wantLinkCheck) {
        mSurveyDownlinkCnt++;
        uint8_t margin = 0;
        uint8_t gwCnt = 0;
        if (node.getMacLinkCheckAns(&margin, &gwCnt) == RADIOLIB_ERR_NONE) {
            answered = true;
            mSurveyLastMargin = margin;
            mSurveyLastGwCnt = gwCnt;
            captureSignalQuality();
            ESP_LOGW(TAG, "[survey] link check (%s): margin %u dB, %u gateway(s), SF%u, fCnt %u",
                     trigger, margin, gwCnt, mSurveySF, uplinkDetails.fCnt);
        } else {
            // Either the gateway did not hear the uplink, or we did not hear the answer. The
            // uplink is the weaker direction in practice, so this almost always means out of
            // range - but it is genuinely ambiguous and is recorded as "asked, no answer".
            mSurveyLastMargin = 0xFF;
            mSurveyLastGwCnt = 0;
            ESP_LOGW(TAG, "[survey] link check (%s): NO ANSWER at SF%u, fCnt %u",
                     trigger, mSurveySF, uplinkDetails.fCnt);
        }
        surveyWriteCsvRow("check", trigger, hasFix, gpsInfo.lat, gpsInfo.lng,
                          uplinkDetails.fCnt, answered);
        surveyPlayReadout(answered ? surveyLevelFromMargin(static_cast<int>(mSurveyLastMargin)) : 0,
                          answered ? mSurveyLastGwCnt : 0);
        surveyAdaptSpreadingFactor(answered, mSurveyLastMargin);
    } else {
        surveyWriteCsvRow("tx", trigger, hasFix, gpsInfo.lat, gpsInfo.lng,
                          uplinkDetails.fCnt, false);
    }

    saveSessionToRTC();
    return true;
}

void ElocLora::surveyAdaptSpreadingFactor(bool answered, uint8_t margin) {
    if (!getSurveyConfig().adaptiveSF) {
        return;
    }

    // Find where we are on the ladder.
    int rung = 0;
    for (unsigned i = 0; i < sizeof(C_SURVEY_SF_LADDER); i++) {
        if (C_SURVEY_SF_LADDER[i] == mSurveySF) {
            rung = static_cast<int>(i);
            break;
        }
    }

    int newRung = rung;
    if (!answered) {
        mSurveyGoodChecks = 0;
        if (++mSurveyMissedChecks >= C_SURVEY_SF_STEP_THRESHOLD) {
            mSurveyMissedChecks = 0;
            // Step towards more range. Each rung buys 2.5-5 dB of link budget and costs airtime,
            // which is exactly the trade worth making at the edge and nowhere else.
            if (newRung < static_cast<int>(sizeof(C_SURVEY_SF_LADDER)) - 1) newRung++;
        }
    } else if (margin >= C_SURVEY_HEALTHY_MARGIN_DB) {
        mSurveyMissedChecks = 0;
        if (++mSurveyGoodChecks >= C_SURVEY_SF_STEP_THRESHOLD) {
            mSurveyGoodChecks = 0;
            // Back towards SF7: cheaper airtime and a denser map wherever coverage allows it.
            if (newRung > 0) newRung--;
        }
    } else {
        // Answered but thin. Hold station - this is the boundary we came to map.
        mSurveyMissedChecks = 0;
        mSurveyGoodChecks = 0;
    }

    if (newRung != rung) {
        mSurveySF = C_SURVEY_SF_LADDER[newRung];
        ESP_LOGW(TAG, "[survey] stepping to SF%u", mSurveySF);
        surveyApplyRadioSettings();
    }
}

/*****************************************************************************************
 * Loop
 *****************************************************************************************/

void ElocLora::surveyLoop() {
    const surveyConfig_t& cfg = getSurveyConfig();

    // The config flag is what survives a reboot, so the session follows it in both directions.
    if (!cfg.enable) {
        if (mSurveyActive) surveyEndSession("disabled in config");
        mSurveyStartFailed = false;
        return;
    }
    if (!mSurveyActive) {
        if (mSurveyStartFailed) {
            return;  // already tried and failed this boot; do not hammer the SD card every loop
        }
        surveyStart();
        return;
    }

    // Hard stop. A survey that survives a reboot can also survive being forgotten, and this mode
    // transmits far more than normal operation and holds the GPS on continuously.
    const int64_t elapsedS = timeObject.getLocalEpoch() - mSurveyStartS;
    if (elapsedS >= static_cast<int64_t>(cfg.sessionTimeoutMin) * 60) {
        surveyEndSession("session timeout");
        return;
    }

    // On-demand triggers first: a ranger standing at a candidate tree should not wait for the
    // schedule. The app button asks for a downlink (it wants an answer); GPIO0 does not (it just
    // drops a point on the map, and the button is the one anyone can press by accident).
    if (mSurveyForceCheck) {
        mSurveyForceCheck = false;
        surveySend(true, "app");
        return;
    }
    if (mSurveyForceUplink) {
        mSurveyForceUplink = false;
        if (cfg.buttonUplink) {
            surveySend(false, "button");
        }
        return;
    }

    // Automatic sampling: distance, gated by the time floor.
    const int64_t nowMs = esp_timer_get_time() / 1000;
    const uint32_t sinceS = (mSurveyLastTxMs == 0)
                          ? 0xFFFFFFFFu
                          : static_cast<uint32_t>((nowMs - mSurveyLastTxMs) / 1000);
    if (sinceS < surveyMinIntervalS()) {
        return;
    }

    portENTER_CRITICAL(&mGpsInfoMux);
    GpsInfo_t gpsInfo = mGpsInfo;
    portEXIT_CRITICAL(&mGpsInfoMux);

    bool due = false;
    const char* trigger = "periodic";
    if (!mSurveyHasAnchor) {
        // Nothing sent yet this session - send once so the track has a start, fix or no fix.
        due = true;
    } else if (gpsInfo.hasFix && !mSurveyHasPosition) {
        // First real fix of the session, after starting without one. Worth a point immediately:
        // it anchors the track, and waiting out the idle backstop would throw away up to ten
        // minutes of walking.
        due = true;
        trigger = "firstfix";
    } else if (gpsInfo.hasFix && mSurveyHasPosition &&
               surveyDistanceM(mSurveyLastLat, mSurveyLastLng, gpsInfo.lat, gpsInfo.lng)
                   >= static_cast<double>(cfg.minDistanceM)) {
        due = true;
    } else if (sinceS >= C_SURVEY_IDLE_INTERVAL_S) {
        // Standing still is nearly free by design, but not literally invisible. This is also what
        // a fixless session falls back to, rather than the interval floor.
        due = true;
        trigger = "idle";
    }

    if (!due) {
        return;
    }

    // Every Nth sample also carries a LinkCheckReq. It doubles as the ranger's ambient reading and
    // as the only feedback the adaptive ladder gets - in an uplink-first survey the device is
    // otherwise blind and would never learn it had left coverage.
    const bool wantCheck = (cfg.linkCheckEveryN > 0) &&
                           ((mSurveySampleCnt % cfg.linkCheckEveryN) == 0);
    surveySend(wantCheck, trigger);
}

/*****************************************************************************************
 * Status
 *****************************************************************************************/

void ElocLora::surveyStatusJson(String& out) {
    const surveyConfig_t& cfg = getSurveyConfig();
    const int level = (mSurveyLastMargin == 0xFF) ? 0
                                                  : surveyLevelFromMargin(static_cast<int>(mSurveyLastMargin));
    char buf[420];
    snprintf(buf, sizeof(buf),
             "\"active\":%s,\"sf\":%u,\"level\":%d,\"marginDb\":%d,\"gwCnt\":%u,"
             "\"samples\":%u,\"elapsedS\":%lld,\"uplinksToday\":%u,\"downlinksToday\":%u,"
             "\"maxUplinksPerDay\":%u,\"maxDownlinksPerDay\":%u,"
             "\"minIntervalS\":%u,\"minDistanceM\":%u,\"file\":\"%s\"",
             mSurveyActive ? "true" : "false",
             mSurveySF, level,
             (mSurveyLastMargin == 0xFF) ? -1 : static_cast<int>(mSurveyLastMargin),
             mSurveyLastGwCnt, mSurveySampleCnt,
             mSurveyActive ? (long long)(timeObject.getLocalEpoch() - mSurveyStartS) : 0LL,
             mSurveyUplinkCnt, mSurveyDownlinkCnt,
             cfg.maxUplinksPerDay, cfg.maxDownlinksPerDay,
             surveyMinIntervalS(), cfg.minDistanceM,
             mSurveyCsvPath.c_str());
    out += buf;
}

/*****************************************************************************************
 * Export to KML / GPX
 *
 * Locus Map and Avenza both read KML and GPX natively; neither imports a raw CSV without a
 * fight. So the CSV stays the authoritative record (and the half of the dead-spot join that
 * only exists on the card), and these two are what actually get opened in the field.
 *
 * Generated at session close, and re-runnable over any session file afterwards - the CSV is
 * append-only during the survey precisely so a run that ended badly can still be exported.
 *****************************************************************************************/

/// Column indices in the survey CSV. Must track the header written by surveyBeginSession().
/// CSV_CLOCK is appended last on purpose: every existing index keeps its position, so a session
/// file written by an older firmware still parses here (surveySplitCsv just returns one field
/// fewer, and every read below is already guarded on the field count).
enum SurveyCsvCol {
    CSV_TIME = 0, CSV_LAT, CSV_LON, CSV_SATS, CSV_KIND, CSV_SF,
    CSV_FCNT, CSV_MARGIN, CSV_GWCNT, CSV_LEVEL, CSV_TRIGGER, CSV_CLOCK,
    CSV_COLS
};

/// KML placemark colours per level, aabbggrr (KML byte order is the reverse of HTML).
/// Level 0 (no answer) is deliberately the most visible: on a coverage survey the dead spots
/// are the finding, not the background.
static const char* C_SURVEY_KML_COLOR[6] = {
    "ff2222dd",  // 0 no link   - red
    "ff2277ee",  // 1 marginal  - orange
    "ff22ccee",  // 2 weak      - yellow
    "ff44cc44",  // 3 usable    - green
    "ff88bb22",  // 4 good      - teal-green
    "ffcc9922",  // 5 excellent - blue-green
};
static const char* C_SURVEY_LEVEL_NAME[6] = {
    "no link", "marginal", "weak", "usable", "good", "excellent"
};

int ElocLora::surveySplitCsv(char* line, const char** fields, int maxCols) {
    int n = 0;
    char* p = line;
    fields[n++] = p;
    while ((*p != '\0') && (n < maxCols)) {
        if (*p == ',') {
            *p = '\0';
            fields[n++] = p + 1;
        }
        p++;
    }
    // Trim the trailing newline off whatever turned out to be the last field.
    char* end = const_cast<char*>(fields[n - 1]);
    while (*end != '\0') {
        if ((*end == '\r') || (*end == '\n')) { *end = '\0'; break; }
        end++;
    }
    return n;
}

bool ElocLora::surveyRowPosition(const char** fields, int nFields, double& lat, double& lng) {
    if (nFields <= CSV_LON) {
        return false;
    }
    // A row logged without a GPS fix has empty lat/lon fields. Plotting those would drop the
    // point at 0,0 in the Gulf of Guinea, which is worse than omitting it.
    if ((fields[CSV_LAT][0] == '\0') || (fields[CSV_LON][0] == '\0')) {
        return false;
    }
    lat = atof(fields[CSV_LAT]);
    lng = atof(fields[CSV_LON]);
    return true;
}

esp_err_t ElocLora::surveyExport(const char* csvPath) {
    String path = (csvPath != nullptr) ? String(csvPath) : mSurveyCsvPath;
    if (path.length() == 0) {
        ESP_LOGE(TAG, "[survey] export: no session file");
        return ESP_ERR_NOT_FOUND;
    }
    if (!sd_card.claimFs()) {
        ESP_LOGE(TAG, "[survey] export: SD busy");
        return ESP_FAIL;
    }
    if (!sd_card.isMounted()) {
        sd_card.releaseFs();
        return ESP_FAIL;
    }

    String base = path;
    if (base.endsWith(".csv")) {
        base = base.substring(0, base.length() - 4);
    }
    const String kmlPath = base + ".kml";
    const String gpxPath = base + ".gpx";

    FILE* in = fopen(path.c_str(), "r");
    if (in == nullptr) {
        ESP_LOGE(TAG, "[survey] export: cannot read %s", path.c_str());
        sd_card.releaseFs();
        return ESP_ERR_NOT_FOUND;
    }
    FILE* kml = fopen(kmlPath.c_str(), "w");
    FILE* gpx = fopen(gpxPath.c_str(), "w");
    if ((kml == nullptr) || (gpx == nullptr)) {
        if (kml != nullptr) fclose(kml);
        if (gpx != nullptr) fclose(gpx);
        fclose(in);
        ESP_LOGE(TAG, "[survey] export: cannot create %s / .gpx", kmlPath.c_str());
        sd_card.releaseFs();
        return ESP_FAIL;
    }

    // Document name is the bare session filename, which is what shows in the Locus/Avenza layer list.
    const char* name = base.c_str();
    const char* slash = strrchr(name, '/');
    if (slash != nullptr) {
        name = slash + 1;
    }

    fprintf(kml, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                 "<kml xmlns=\"http://www.opengis.net/kml/2.2\"><Document>\n"
                 "<name>%s</name>\n", name);
    for (int lvl = 0; lvl <= 5; lvl++) {
        fprintf(kml,
                "<Style id=\"L%d\"><IconStyle><color>%s</color><scale>1.1</scale>"
                "<Icon><href>http://maps.google.com/mapfiles/kml/shapes/placemark_circle.png</href></Icon>"
                "</IconStyle></Style>\n",
                lvl, C_SURVEY_KML_COLOR[lvl]);
    }
    fprintf(kml, "<Style id=\"track\"><LineStyle><color>ffdd8800</color>"
                 "<width>3</width></LineStyle></Style>\n");
    fprintf(kml, "<Folder><name>Samples</name>\n");

    fprintf(gpx, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                 "<gpx version=\"1.1\" creator=\"ELOC\" "
                 "xmlns=\"http://www.topografix.com/GPX/1/1\">\n"
                 "<metadata><name>%s</name></metadata>\n", name);

    char line[256];
    const char* fields[CSV_COLS];
    uint32_t points = 0;
    uint32_t checks = 0;
    bool firstLine = true;

    // Pass 1: the measured points, as KML placemarks and GPX waypoints.
    while (fgets(line, sizeof(line), in) != nullptr) {
        if (firstLine) {
            firstLine = false;
            continue;   // header
        }
        const int n = surveySplitCsv(line, fields, CSV_COLS);
        double lat, lng;
        if (!surveyRowPosition(fields, n, lat, lng)) {
            continue;
        }
        points++;

        // Only link-check rows carry a measured level. Plain uplinks are positions we transmitted
        // from and know nothing else about - they belong on the track, not as graded placemarks.
        // Their verdict comes later, from joining fcnt against what the server actually received.
        const bool isCheck = (n > CSV_KIND) && (strcmp(fields[CSV_KIND], "check") == 0);
        if (!isCheck) {
            continue;
        }
        checks++;

        int level = 0;
        if ((n > CSV_LEVEL) && (fields[CSV_LEVEL][0] != '\0')) {
            level = atoi(fields[CSV_LEVEL]);
        }
        if ((level < 0) || (level > 5)) {
            level = 0;
        }
        const char* margin = ((n > CSV_MARGIN) && (fields[CSV_MARGIN][0] != '\0'))
                           ? fields[CSV_MARGIN] : "-";
        const char* gwCnt = ((n > CSV_GWCNT) && (fields[CSV_GWCNT][0] != '\0'))
                          ? fields[CSV_GWCNT] : "0";
        const char* sf = (n > CSV_SF) ? fields[CSV_SF] : "?";

        fprintf(kml,
                "<Placemark><name>%d %s</name><styleUrl>#L%d</styleUrl>"
                "<description>%s | SF%s | margin %s dB | %s gateway(s)</description>"
                "<Point><coordinates>%.6f,%.6f</coordinates></Point></Placemark>\n",
                level, C_SURVEY_LEVEL_NAME[level], level,
                fields[CSV_TIME], sf, margin, gwCnt, lng, lat);

        fprintf(gpx,
                "<wpt lat=\"%.6f\" lon=\"%.6f\"><name>L%d %s dB</name>"
                "<desc>%s | SF%s | %s gateway(s)</desc></wpt>\n",
                lat, lng, level, margin, fields[CSV_TIME], sf, gwCnt);
    }

    // Pass 2: the route, through every transmission that had a fix. Re-reading the file beats
    // holding a few hundred coordinate pairs in RAM on a part with no headroom, and a LineString
    // has to be emitted as one contiguous block.
    fprintf(kml, "</Folder>\n<Placemark><name>Route</name><styleUrl>#track</styleUrl>"
                 "<LineString><tessellate>1</tessellate><coordinates>");
    fprintf(gpx, "<trk><name>%s</name><trkseg>\n", name);

    rewind(in);
    firstLine = true;
    while (fgets(line, sizeof(line), in) != nullptr) {
        if (firstLine) {
            firstLine = false;
            continue;
        }
        const int n = surveySplitCsv(line, fields, CSV_COLS);
        double lat, lng;
        if (!surveyRowPosition(fields, n, lat, lng)) {
            continue;
        }
        fprintf(kml, "%.6f,%.6f ", lng, lat);
        fprintf(gpx, "<trkpt lat=\"%.6f\" lon=\"%.6f\"></trkpt>\n", lat, lng);
    }
    fclose(in);

    fprintf(kml, "</coordinates></LineString></Placemark>\n</Document></kml>\n");
    fprintf(gpx, "</trkseg></trk>\n</gpx>\n");
    fclose(kml);
    fclose(gpx);
    sd_card.releaseFs();

    ESP_LOGW(TAG, "[survey] exported %u points (%u measured) -> %s + .gpx",
             points, checks, kmlPath.c_str());
    return ESP_OK;
}
