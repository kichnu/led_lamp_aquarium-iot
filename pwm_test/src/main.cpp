// RL90 — test sterowania 4 kanałami PWM (Seeed XIAO ESP32-C3)
//
// AP + captive portal (DNS) -> telefon łączy się z siecią RL90-PWM-TEST i dostaje GUI.
// Kanały A-D: LEDC 14 bit, częstotliwość regulowana z GUI (wspólny timer).
// Wyjścia prowadzone bezpośrednio przez rezystor szeregowy 1 kΩ do padów A-D lampy.
//
// Test gpio_hold (restart bez błysku): GUI ma dwa przyciski restartu. Oba najpierw gaszą A-D (0%),
// potem ESP.restart(). "z hold" dodatkowo zatrzaskuje piny w LOW (gpio_hold_en) — jeśli hold
// przetrwa reset programowy C3, lampa zostaje ciemna przez cały boot; "bez hold" = wzorzec
// porównawczy (pull-up Hi7001 → ~1 s błysku 100%). Hold zwalniany po initPwm() (LEDC już na 0%).

#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <driver/ledc.h>
#include <driver/gpio.h>
#include <esp_system.h>

// ===== Piny (XIAO ESP32-C3, omijamy strapping GPIO2/8/9) =====
static const int NUM_CH = 4;
static const int CH_PIN[NUM_CH]  = {3, 4, 5, 6};   // D1, D2, D3, D4 -> pady A, B, C, D
static const char CH_NAME[NUM_CH] = {'A', 'B', 'C', 'D'};

// ===== PWM =====
static const ledc_timer_bit_t PWM_RES = LEDC_TIMER_14_BIT;
static const uint32_t PWM_MAX   = 1u << 14;         // duty = 2^res => 100% (bez impulsu LOW)
static const uint32_t FREQ_MIN  = 100;
static const uint32_t FREQ_MAX  = 2000;
static const uint32_t FREQ_DEF  = 1000;

// ===== AP =====
static const char* AP_SSID = "RL90-PWM-TEST";
static const char* AP_PASS = "pwmtest123";
static const int   AP_CHANNEL = 6;

static float    g_pct[NUM_CH] = {0, 0, 0, 0};
static bool     g_on   = false;
static uint32_t g_freq = FREQ_DEF;

// Przeżywa reset programowy (nie power-on) — znacznik, że restart był z hold
static const uint32_t HOLD_MAGIC = 0x484F4C44;     // "HOLD"
RTC_NOINIT_ATTR static uint32_t g_holdMagic;
static bool     g_bootWithHold = false;
static uint32_t g_restartAt = 0;                   // millis() zaplanowanego restartu, 0 = brak
static bool     g_restartHold = false;

static DNSServer dnsServer;
static AsyncWebServer server(80);

// ---------------------------------------------------------------
// PWM
// ---------------------------------------------------------------
static void applyChannel(int ch) {
    float pct = g_on ? g_pct[ch] : 0.0f;
    uint32_t duty = (uint32_t)(pct / 100.0f * PWM_MAX + 0.5f);
    if (duty > PWM_MAX) duty = PWM_MAX;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)ch, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)ch);
}

static void applyAll() {
    for (int i = 0; i < NUM_CH; i++) applyChannel(i);
}

static void initPwm() {
    ledc_timer_config_t timer = {};
    timer.speed_mode      = LEDC_LOW_SPEED_MODE;
    timer.duty_resolution = PWM_RES;
    timer.timer_num       = LEDC_TIMER_0;
    timer.freq_hz         = g_freq;
    timer.clk_cfg         = LEDC_AUTO_CLK;
    ledc_timer_config(&timer);

    for (int i = 0; i < NUM_CH; i++) {
        ledc_channel_config_t c = {};
        c.gpio_num   = CH_PIN[i];
        c.speed_mode = LEDC_LOW_SPEED_MODE;
        c.channel    = (ledc_channel_t)i;
        c.timer_sel  = LEDC_TIMER_0;
        c.duty       = 0;
        c.hpoint     = 0;
        ledc_channel_config(&c);
    }
}

static const char* resetReasonStr() {
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON:  return "POWERON";
        case ESP_RST_SW:       return "SW";
        case ESP_RST_PANIC:    return "PANIC";
        case ESP_RST_INT_WDT:  return "INT_WDT";
        case ESP_RST_TASK_WDT: return "TASK_WDT";
        case ESP_RST_WDT:      return "WDT";
        case ESP_RST_BROWNOUT: return "BROWNOUT";
        case ESP_RST_USB:      return "USB";
        default:               return "OTHER";
    }
}

// Wygaszenie A-D stałym LOW (ledc_stop z idle=0, bez PWM), opcjonalnie hold, restart
static void doRestart(bool hold) {
    for (int i = 0; i < NUM_CH; i++) ledc_stop(LEDC_LOW_SPEED_MODE, (ledc_channel_t)i, 0);
    if (hold) {
        for (int i = 0; i < NUM_CH; i++) gpio_hold_en((gpio_num_t)CH_PIN[i]);
        g_holdMagic = HOLD_MAGIC;
    } else {
        g_holdMagic = 0;
    }
    Serial.printf("RESTART %s hold\n", hold ? "Z" : "BEZ");
    Serial.flush();
    delay(50);
    ESP.restart();
}

static void setFreq(uint32_t hz) {
    g_freq = constrain(hz, FREQ_MIN, FREQ_MAX);
    ledc_set_freq(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0, g_freq);
    applyAll();
}

// ---------------------------------------------------------------
// GUI
// ---------------------------------------------------------------
static const char PAGE[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="pl"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,user-scalable=no">
<title>RL90 PWM test</title>
<style>
:root{--bg:#0f1115;--card:#1a1d24;--fg:#e8eaed;--mut:#8b93a1;--acc:#4ea1ff;--on:#2fbf71;--off:#c0392b}
*{box-sizing:border-box}
body{margin:0;padding:16px;background:var(--bg);color:var(--fg);font-family:system-ui,sans-serif;max-width:480px;margin:auto}
h1{font-size:18px;margin:0 0 12px}
.card{background:var(--card);border-radius:12px;padding:14px;margin-bottom:12px}
button{border:0;border-radius:10px;background:#2b303b;color:var(--fg);font-size:26px;font-weight:600;
  min-width:64px;min-height:56px;touch-action:manipulation}
button:active{background:var(--acc)}
#pwr{width:100%;font-size:20px;min-height:60px;background:var(--off)}
#pwr.on{background:var(--on)}
.row{display:flex;align-items:center;gap:10px}
.lbl{font-size:20px;font-weight:700;width:28px}
.val{flex:1;text-align:center;font-size:30px;font-variant-numeric:tabular-nums}
.val input{width:100%;background:transparent;border:0;color:var(--fg);font:inherit;text-align:center}
.bar{height:6px;background:#2b303b;border-radius:3px;margin-top:10px;overflow:hidden}
.bar i{display:block;height:100%;background:var(--acc)}
.sub{color:var(--mut);font-size:12px;margin-top:6px}
.f input{width:90px;font-size:18px;padding:8px;border-radius:8px;border:1px solid #2b303b;background:#0f1115;color:var(--fg)}
.f button{font-size:16px;min-height:40px;min-width:60px}
</style></head><body>
<h1>RL90 — test PWM</h1>
<div class="card"><button id="pwr">OFF</button></div>
<div id="chs"></div>
<div class="card f"><div class="row">
<span>Częstotliwość [Hz]</span>
<input id="freq" type="number" inputmode="numeric" min="100" max="2000" step="50">
<button id="fset">Ustaw</button></div>
<div class="sub">Zakres 100–2000 Hz, 14 bit, wspólna dla A–D</div></div>
<div class="card f"><div class="row">
<button id="rsth" style="flex:1">Restart z hold</button>
<button id="rstn" style="flex:1">Restart bez hold</button></div>
<div class="sub">Oba gaszą A–D przed restartem. Z hold: lampa powinna zostać ciemna przez cały boot.
Bez hold: oczekiwany ~1 s błysk 100%. Po restarcie połącz się ponownie z AP.</div>
<div class="sub" id="boot"></div></div>
<script>
const N=4, names=['A','B','C','D'], pins=[3,4,5,6];
let st={on:false,freq:1000,ch:[0,0,0,0]};
const $=id=>document.getElementById(id);
const chs=$('chs');
for(let i=0;i<N;i++){
  chs.insertAdjacentHTML('beforeend',
   `<div class="card"><div class="row"><span class="lbl">${names[i]}</span>
    <button data-c="${i}" data-d="-1">−</button>
    <span class="val"><input id="v${i}" type="number" inputmode="decimal" min="0" max="100" step="0.01"></span>
    <button data-c="${i}" data-d="1">+</button></div>
    <div class="bar"><i id="b${i}"></i></div><div class="sub">GPIO${pins[i]} · %</div></div>`);
}
function post(url,body){
  return fetch(url,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},
    body:new URLSearchParams(body)}).then(r=>r.json()).then(s=>{st=s;render(true)}).catch(()=>{});
}
function render(force){
  $('pwr').textContent=st.on?'ON':'OFF';
  $('pwr').className=st.on?'on':'';
  for(let i=0;i<N;i++){
    const inp=$('v'+i);
    if(force||document.activeElement!==inp) inp.value=(+st.ch[i]).toFixed(2).replace(/\.?0+$/,'');
    $('b'+i).style.width=st.ch[i]+'%';
  }
  if(force||document.activeElement!==$('freq')) $('freq').value=st.freq;
  $('boot').textContent='Ostatni reset: '+st.reset+(st.hold_boot?' (z hold)':'');
}
function setCh(i,v){v=Math.max(0,Math.min(100,+v||0));post('/api/set',{ch:i,pct:v});}
document.addEventListener('click',e=>{
  const b=e.target.closest('button[data-c]'); if(!b) return;
  const i=+b.dataset.c; setCh(i,Math.round(st.ch[i])+(+b.dataset.d));
});
for(let i=0;i<N;i++) $('v'+i).addEventListener('change',e=>setCh(i,e.target.value));
$('pwr').onclick=()=>post('/api/power',{on:st.on?0:1});
$('fset').onclick=()=>post('/api/freq',{hz:$('freq').value});
$('rsth').onclick=()=>post('/api/restart',{hold:1});
$('rstn').onclick=()=>post('/api/restart',{hold:0});
function poll(){fetch('/api/state').then(r=>r.json()).then(s=>{st=s;render(false)}).catch(()=>{});}
poll(); setInterval(poll,3000);
</script></body></html>)HTML";

static String stateJson() {
    String s = "{\"on\":";
    s += g_on ? "true" : "false";
    s += ",\"freq\":" + String(g_freq) + ",\"ch\":[";
    for (int i = 0; i < NUM_CH; i++) {
        if (i) s += ",";
        s += String(g_pct[i], 2);
    }
    s += "],\"reset\":\"";
    s += resetReasonStr();
    s += "\",\"hold_boot\":";
    s += g_bootWithHold ? "true" : "false";
    s += "}";
    return s;
}

static void sendState(AsyncWebServerRequest* r) {
    r->send(200, "application/json", stateJson());
}

static void setupServer() {
    server.on("/", HTTP_GET, [](AsyncWebServerRequest* r) { r->send(200, "text/html", PAGE); });
    server.on("/api/state", HTTP_GET, sendState);

    server.on("/api/set", HTTP_POST, [](AsyncWebServerRequest* r) {
        if (!r->hasParam("ch", true) || !r->hasParam("pct", true)) { r->send(400, "text/plain", "bad"); return; }
        int ch = r->getParam("ch", true)->value().toInt();
        float pct = r->getParam("pct", true)->value().toFloat();
        if (ch < 0 || ch >= NUM_CH) { r->send(400, "text/plain", "bad ch"); return; }
        g_pct[ch] = constrain(pct, 0.0f, 100.0f);
        applyChannel(ch);
        Serial.printf("CH %c = %.2f%%\n", CH_NAME[ch], g_pct[ch]);
        sendState(r);
    });

    server.on("/api/power", HTTP_POST, [](AsyncWebServerRequest* r) {
        if (!r->hasParam("on", true)) { r->send(400, "text/plain", "bad"); return; }
        g_on = r->getParam("on", true)->value().toInt() != 0;
        applyAll();
        Serial.printf("POWER %s\n", g_on ? "ON" : "OFF");
        sendState(r);
    });

    server.on("/api/freq", HTTP_POST, [](AsyncWebServerRequest* r) {
        if (!r->hasParam("hz", true)) { r->send(400, "text/plain", "bad"); return; }
        setFreq(r->getParam("hz", true)->value().toInt());
        Serial.printf("FREQ %u Hz\n", (unsigned)g_freq);
        sendState(r);
    });

    // Restart wykonywany z loop() — nie z handlera async (odpowiedź musi zdążyć wyjść)
    server.on("/api/restart", HTTP_POST, [](AsyncWebServerRequest* r) {
        g_restartHold = r->hasParam("hold", true) && r->getParam("hold", true)->value().toInt() != 0;
        g_restartAt = millis() + 300;
        sendState(r);
    });

    // Captive portal — iOS / Android / Windows
    const char* probes[] = {"/hotspot-detect.html", "/library/test/success.html", "/generate_204",
                            "/gen_204", "/connecttest.txt", "/ncsi.txt"};
    for (const char* p : probes)
        server.on(p, HTTP_GET, [](AsyncWebServerRequest* r) { r->redirect("/"); });
    server.onNotFound([](AsyncWebServerRequest* r) { r->redirect("/"); });

    server.begin();
}

// ---------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    initPwm();          // najpierw PWM — wszystkie kanały 0%

    // Zwolnienie hold dopiero gdy LEDC już trzyma 0% — inaczej pad na chwilę bez sterowania (błysk).
    // gpio_hold_dis zawsze (nieszkodliwe bez hold), znacznik tylko do diagnostyki.
    g_bootWithHold = (esp_reset_reason() == ESP_RST_SW && g_holdMagic == HOLD_MAGIC);
    for (int i = 0; i < NUM_CH; i++) gpio_hold_dis((gpio_num_t)CH_PIN[i]);
    g_holdMagic = 0;

    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASS, AP_CHANNEL, 0, 4);
    delay(300);
    IPAddress ip = WiFi.softAPIP();
    dnsServer.start(53, "*", ip);
    setupServer();

    Serial.printf("\nAP: %s / %s  IP: %s\n", AP_SSID, AP_PASS, ip.toString().c_str());
    Serial.printf("Reset: %s%s\n", resetReasonStr(), g_bootWithHold ? " (z hold)" : "");
}

void loop() {
    dnsServer.processNextRequest();
    if (g_restartAt && (int32_t)(millis() - g_restartAt) >= 0) doRestart(g_restartHold);
    delay(5);
}
