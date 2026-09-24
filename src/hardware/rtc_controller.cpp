#include "rtc_controller.h"
#include "../core/logging.h"
#include "../core/lamp_lock.h"
#include "../hardware/hardware_pins.h"
#include <Wire.h>
#include <RTClib.h>
#include <time.h>
#include <sys/time.h>
#include "esp_sntp.h"

// Serwery po IP — VLAN IoT bywa bez sprawnego DNS (wzorzec z termostatu)
static const char* NTP_SERVER_1 = "216.239.35.0";    // time.google.com
static const char* NTP_SERVER_2 = "216.239.35.4";    // time2.google.com
static const char* NTP_SERVER_3 = "162.159.200.1";   // time.cloudflare.com

const char* POLAND_TZ = "CET-1CEST,M3.5.0,M10.5.0/3";

#define TIME_VALID_MIN 1700000000UL   // 2023-11

static RTC_DS3231 rtc;
static bool rtcPresent = false;
static bool rtcTimeOk = false;
static bool batteryIssue = false;
static volatile bool ntpSyncPending = false;
static unsigned long lastNtpSyncMs = 0;
static bool ntpEverSynced = false;

static void onNtpSync(struct timeval* tv) {
    ntpSyncPending = true;   // callback w tasku lwIP — zapis do DS3231 w loop()
}

void initializeRTC() {
    setenv("TZ", POLAND_TZ, 1);
    tzset();

    LampLock lock;
    Wire.beginTransmission(RTC_I2C_ADDR);
    if (Wire.endTransmission() != 0 || !rtc.begin()) {
        LOG_ERROR("DS3231 nie znaleziony — czas tylko z NTP");
        return;
    }
    rtcPresent = true;

    if (rtc.lostPower()) {
        LOG_WARNING("DS3231: utrata zasilania (bateria?) — czas niepewny do synchronizacji NTP");
        batteryIssue = true;
        return;
    }

    DateTime now = rtc.now();
    if (now.year() < 2024 || now.year() > 2099) {
        LOG_ERROR("DS3231: nieprawidłowy rok %d", now.year());
        return;
    }

    struct timeval tv = { (time_t)now.unixtime(), 0 };
    settimeofday(&tv, nullptr);
    rtcTimeOk = true;
    LOG_INFO("Zegar systemowy z DS3231: %s", getCurrentTimestamp().c_str());
}

void startNTP() {
    sntp_set_time_sync_notification_cb(onNtpSync);
    configTzTime(POLAND_TZ, NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);
    LOG_INFO("SNTP uruchomiony w tle");
}

void updateRTCSync() {
    if (!ntpSyncPending) return;
    ntpSyncPending = false;
    lastNtpSyncMs = millis();
    ntpEverSynced = true;

    time_t now = time(nullptr);
    if (rtcPresent) {
        LampLock lock;
        rtc.adjust(DateTime((uint32_t)now));   // UTC, bez offsetów
        rtcTimeOk = true;
        batteryIssue = false;
    }
    LOG_INFO("NTP sync: %s%s", getCurrentTimestamp().c_str(), rtcPresent ? " (zapisano do DS3231)" : "");
}

bool isTimeValid() {
    return (unsigned long)time(nullptr) > TIME_VALID_MIN;
}

bool isRTCWorking() {
    return rtcPresent && rtcTimeOk;
}

bool isBatteryIssueDetected() {
    return batteryIssue;
}

unsigned long getUnixTimestamp() {
    return (unsigned long)time(nullptr);
}

String getCurrentTimestamp() {
    if (!isTimeValid()) return "--";
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    char buf[24];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &t);
    return String(buf);
}

String getRTCInfo() {
    if (!rtcPresent) return isTimeValid() ? "NTP (brak DS3231)" : "brak czasu";
    if (batteryIssue) return "DS3231 (bateria? czeka na NTP)";
    if (!rtcTimeOk) return "DS3231 (nieprawidłowy czas)";
    return ntpEverSynced ? "DS3231 + NTP" : "DS3231";
}

uint32_t getLastNtpSyncAgeS() {
    if (!ntpEverSynced) return UINT32_MAX;
    return (millis() - lastNtpSyncMs) / 1000UL;
}
