#ifndef SYS_INFO_H
#define SYS_INFO_H

#include <Arduino.h>

const char* resetReasonStr(uint8_t reason);   // esp_reset_reason_t → tekst

// Zlicza przyczynę bieżącego startu w SYSTEM_STATE (FRAM) i zapisuje ją jako ostatnią
void recordResetReason();

#endif
