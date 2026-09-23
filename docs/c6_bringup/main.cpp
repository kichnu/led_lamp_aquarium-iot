// RL90 bring-up test — Seeed XIAO ESP32-C6, Arduino core 3.x
// Komendy (Serial 115200, zakończone Enter):
//   a 12.5      -> kanał A na 12.5 %   (a,b,c,d,f)
//   all 0       -> wszystkie kanały LED na 0 %
//   ramp c      -> kanał c po krokach log (3 s/krok), reszta LED = 0
//   freq 500    -> zmiana częstotliwości PWM kanałów LED [Hz]
//   fan on      -> odblokowanie kanału F (dopiero po pomiarze napięcia na FAN!)
#include <Arduino.h>

struct Ch { const char *name; uint8_t pin; uint32_t freq; uint8_t res; };

Ch ch[] = {
  {"A", D0, 1000, 14},   // GPIO0
  {"B", D1, 1000, 14},   // GPIO1
  {"C", D2, 1000, 14},   // GPIO2  -> 2x Hi7001, grupa niebieska
  {"D", D3, 1000, 14},   // GPIO21
  {"F", D4, 25000, 10},  // GPIO22  -> FAN
};
constexpr int N = sizeof(ch) / sizeof(ch[0]);
constexpr int FAN = 4;
bool fanEnabled = false;

int idxOf(char c) {
  c = toupper(c);
  for (int i = 0; i < N; i++) if (ch[i].name[0] == c) return i;
  return -1;
}

void setDuty(int i, float pct) {
  if (i == FAN && !fanEnabled) { Serial.println("FAN zablokowany (fan on)"); return; }
  pct = constrain(pct, 0.0f, 100.0f);
  uint32_t maxv = (1UL << ch[i].res) - 1;           // core 3.x: maxv => 100 % (pełne ON)
  uint32_t d = lroundf(pct / 100.0f * maxv);
  ledcWrite(ch[i].pin, d);
  Serial.printf("%s = %.3f %%  (%lu/%lu)\n", ch[i].name, pct, (unsigned long)d, (unsigned long)maxv);
}

void allLed(float pct) { for (int i = 0; i < 4; i++) setDuty(i, pct); }

void ramp(int i) {
  static const float steps[] = {0, 0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1, 2, 5, 10, 20, 50, 100};
  allLed(0);
  for (float s : steps) { setDuty(i, s); delay(3000); }
  setDuty(i, 0);
  Serial.println("ramp koniec");
}

void setup() {
  for (int i = 0; i < N; i++) {
    if (i == FAN) continue;                         // FAN pin zostaje high-Z do "fan on"
    ledcAttach(ch[i].pin, ch[i].freq, ch[i].res);
    ledcWrite(ch[i].pin, 0);
  }
  Serial.begin(115200);
  delay(1500);
  Serial.println("RL90 test gotowy. LED = 0 %.");
}

void loop() {
  if (!Serial.available()) return;
  String line = Serial.readStringUntil('\n');
  line.trim(); line.toLowerCase();
  char cmd[8] = {0}; float v = 0;
  int n = sscanf(line.c_str(), "%7s %f", cmd, &v);

  if (!strcmp(cmd, "all") && n == 2) { allLed(v); return; }
  if (!strcmp(cmd, "ramp")) {
    int i = idxOf(line.charAt(line.length() - 1));
    if (i >= 0 && i < 4) ramp(i); else Serial.println("ramp a|b|c|d");
    return;
  }
  if (!strcmp(cmd, "freq") && n == 2) {
    for (int i = 0; i < 4; i++) ledcChangeFrequency(ch[i].pin, (uint32_t)v, ch[i].res);
    Serial.printf("LED PWM = %lu Hz\n", (unsigned long)v);
    return;
  }
  if (line == "fan on") {
    ledcAttach(ch[FAN].pin, ch[FAN].freq, ch[FAN].res);
    fanEnabled = true;
    setDuty(FAN, 100);
    return;
  }
  if (strlen(cmd) == 1 && n == 2) {
    int i = idxOf(cmd[0]);
    if (i >= 0) { setDuty(i, v); return; }
  }
  Serial.println("?  (a|b|c|d|f <pct>, all <pct>, ramp <ch>, freq <hz>, fan on)");
}
