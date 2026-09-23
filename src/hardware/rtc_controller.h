#ifndef RTC_CONTROLLER_H
#define RTC_CONTROLLER_H

#include <Arduino.h>

void initializeRTC();
String getCurrentTimestamp();
bool isRTCWorking();
unsigned long getUnixTimestamp();
String getRTCInfo();

String getTimeSourceInfo();
bool isRTCHardware();

bool rtcNeedsSynchronization();
void initInternalTimeFromCompileTime();
bool setRTCFromNTP();
void updateRTCSync();  // wywołuj co loop() — okresowy resync z NTP (co NTP_SYNC_INTERVAL)

bool isBatteryIssueDetected();

#endif