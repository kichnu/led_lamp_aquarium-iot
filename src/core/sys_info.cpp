#include "sys_info.h"
#include "../lamp/lamp_storage.h"
#include "logging.h"
#include <esp_system.h>

const char* resetReasonStr(uint8_t reason) {
    switch ((esp_reset_reason_t)reason) {
        case ESP_RST_POWERON:   return "POWERON";
        case ESP_RST_EXT:       return "EXT";
        case ESP_RST_SW:        return "SW";
        case ESP_RST_PANIC:     return "PANIC";
        case ESP_RST_INT_WDT:   return "INT_WDT";
        case ESP_RST_TASK_WDT:  return "TASK_WDT";
        case ESP_RST_WDT:       return "WDT";
        case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
        case ESP_RST_BROWNOUT:  return "BROWNOUT";
        case ESP_RST_SDIO:      return "SDIO";
        default:                return "OTHER";
    }
}

void recordResetReason() {
    esp_reset_reason_t r = esp_reset_reason();
    SystemState& s = sysState();
    switch (r) {
        case ESP_RST_INT_WDT:
        case ESP_RST_TASK_WDT:
        case ESP_RST_WDT:      s.cnt_wdt++;      break;
        case ESP_RST_PANIC:    s.cnt_panic++;    break;
        case ESP_RST_BROWNOUT: s.cnt_brownout++; break;
        case ESP_RST_POWERON:
        case ESP_RST_SW:       break;            // normalne: włączenie, restart dobowy/OTA
        default:               s.cnt_other++;    break;
    }
    s.last_reset_reason = (uint8_t)r;
    saveSystemState();
    LOG_INFO("Reset: %s (wdt=%u panic=%u brownout=%u other=%u)", resetReasonStr(r),
             s.cnt_wdt, s.cnt_panic, s.cnt_brownout, s.cnt_other);
}
