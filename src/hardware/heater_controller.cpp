#include "heater_controller.h"
#include "hardware_pins.h"
#include "core/logging.h"

// TODO: Etap 2a — implementacja

static bool s_heater_on = false;

void initHeaterController() {
    pinMode(HEATER_SSR_PIN, OUTPUT);
    digitalWrite(HEATER_SSR_PIN, HIGH);  // active LOW → HIGH = OFF
    s_heater_on = false;
    LOG_INFO("HeaterController: SSR init, pin=%d OFF", HEATER_SSR_PIN);
}

void setHeaterOn() {
    if (!s_heater_on) {
        digitalWrite(HEATER_SSR_PIN, LOW);  // active LOW → ON
        s_heater_on = true;
        LOG_INFO("HeaterController: SSR ON");
    }
}

void setHeaterOff() {
    if (s_heater_on) {
        digitalWrite(HEATER_SSR_PIN, HIGH);
        s_heater_on = false;
        LOG_INFO("HeaterController: SSR OFF");
    }
}

bool isHeaterOn()        { return s_heater_on; }
void heaterEmergencyOff(){ digitalWrite(HEATER_SSR_PIN, HIGH); s_heater_on = false; }
