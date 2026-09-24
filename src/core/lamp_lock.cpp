#include "lamp_lock.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

static SemaphoreHandle_t s_mutex = nullptr;

void initLampLock() {
    if (!s_mutex) s_mutex = xSemaphoreCreateRecursiveMutex();
}

void lampLock() {
    if (!s_mutex) initLampLock();
    xSemaphoreTakeRecursive(s_mutex, portMAX_DELAY);
}

void lampUnlock() {
    xSemaphoreGiveRecursive(s_mutex);
}
