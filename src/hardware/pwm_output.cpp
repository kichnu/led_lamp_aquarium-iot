#include "pwm_output.h"
#include "hardware_pins.h"
#include "../lamp/lamp_types.h"
#include "../core/logging.h"
#include <driver/ledc.h>
#include <driver/gpio.h>
#include <esp_system.h>

static const uint8_t  LED_PINS[NUM_CHANNELS] = { PWM_A_PIN, PWM_B_PIN, PWM_C_PIN, PWM_D_PIN };
static const ledc_channel_t FAN_CHANNEL = LEDC_CHANNEL_4;
static const ledc_timer_t   PWM_TIMER   = LEDC_TIMER_0;
#define HOLD_MAGIC 0x484F4C44  // "HOLD"

// 14 bit to maksimum timera LEDC na C3 — duty = 2^14 przepełnia licznik i daje 0 %.
// Górny limit 2^14 − 1 (99,994 %, szpilka LOW ~0,12 µs na okres).
static const uint32_t DUTY_LIMIT = PWM_MAX_DUTY - 1;

// Przeżywa reset programowy (nie power-on) — znacznik restartu z hold
RTC_NOINIT_ATTR static uint32_t s_holdMagic;

static uint32_t s_ledDuty[NUM_CHANNELS] = { 0 };
static uint32_t s_fanDuty = 0;

static void configChannel(ledc_channel_t ch, uint8_t pin) {
    ledc_channel_config_t c = {};
    c.gpio_num   = pin;
    c.speed_mode = LEDC_LOW_SPEED_MODE;
    c.channel    = ch;
    c.timer_sel  = PWM_TIMER;
    c.duty       = 0;
    c.hpoint     = 0;
    ledc_channel_config(&c);
}

bool initPwmOutputs() {
    // LED i wentylator mają tę samą częstotliwość i rozdzielczość → jeden timer
    static_assert(LED_PWM_FREQ_HZ == FAN_PWM_FREQ_HZ, "różne częstotliwości wymagają drugiego timera LEDC");
    ledc_timer_config_t timer = {};
    timer.speed_mode      = LEDC_LOW_SPEED_MODE;
    timer.duty_resolution = (ledc_timer_bit_t)PWM_RES_BITS;
    timer.timer_num       = PWM_TIMER;
    timer.freq_hz         = LED_PWM_FREQ_HZ;
    timer.clk_cfg         = LEDC_AUTO_CLK;
    ledc_timer_config(&timer);

    for (uint8_t i = 0; i < NUM_CHANNELS; i++) configChannel((ledc_channel_t)i, LED_PINS[i]);
    configChannel(FAN_CHANNEL, FAN_PWM_PIN);

    // Hold zwalniany dopiero gdy LEDC już trzyma 0 % — inaczej pad na chwilę bez sterowania (błysk)
    bool withHold = (esp_reset_reason() == ESP_RST_SW && s_holdMagic == HOLD_MAGIC);
    for (uint8_t i = 0; i < NUM_CHANNELS; i++) gpio_hold_dis((gpio_num_t)LED_PINS[i]);
    s_holdMagic = 0;
    return withHold;
}

void setLedDuty(uint8_t ch, uint32_t duty) {
    if (ch >= NUM_CHANNELS) return;
    if (duty > DUTY_LIMIT) duty = DUTY_LIMIT;
    if (duty == s_ledDuty[ch]) return;
    s_ledDuty[ch] = duty;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)ch, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)ch);
}

uint32_t getLedDuty(uint8_t ch) {
    return ch < NUM_CHANNELS ? s_ledDuty[ch] : 0;
}

void setFanDuty(uint32_t duty) {
    if (duty > DUTY_LIMIT) duty = DUTY_LIMIT;
    if (duty == s_fanDuty) return;
    s_fanDuty = duty;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, FAN_CHANNEL, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, FAN_CHANNEL);
}

uint32_t getFanDuty() {
    return s_fanDuty;
}

void allOutputsOff() {
    for (uint8_t i = 0; i < NUM_CHANNELS; i++) setLedDuty(i, 0);
    setFanDuty(0);
}

void restartWithLedsHeldLow() {
    // ledc_stop z idle=0: pin na stałe LOW (bez PWM), potem zatrzask na czas resetu
    for (uint8_t i = 0; i < NUM_CHANNELS; i++) ledc_stop(LEDC_LOW_SPEED_MODE, (ledc_channel_t)i, 0);
    ledc_stop(LEDC_LOW_SPEED_MODE, FAN_CHANNEL, 0);
    for (uint8_t i = 0; i < NUM_CHANNELS; i++) gpio_hold_en((gpio_num_t)LED_PINS[i]);
    s_holdMagic = HOLD_MAGIC;
    LOG_INFO("=== RESTART (A–D LOW + gpio_hold) ===");
    Serial.flush();
    delay(50);
    ESP.restart();
}
