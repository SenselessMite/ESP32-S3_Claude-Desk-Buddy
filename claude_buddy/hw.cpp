// SPDX-License-Identifier: MIT
#include "hw.h"
#include "board.h"
#include <Arduino.h>
#include <Wire.h>
#include <ESP_I2S.h>
#include <Preferences.h>
#include <time.h>
#define XPOWERS_CHIP_AXP2101
#include <XPowersLib.h>
#include <SensorPCF85063.hpp>

#define IIC_SDA 15
#define IIC_SCL 14

// ===================================================================== audio
// Sequenza ES8311 verificata su questa scheda (vedi TamaPoke): clock derivato dal BCLK.
#define PIN_I2S_BCK 9
#define PIN_I2S_WS 45
#define PIN_I2S_DO 8
#define PIN_I2S_MCK I2S_MCK_PIN
#define PIN_PA 46
#define ES8311_ADDR 0x18
#define SAMPLE_RATE 16000

static I2SClass i2s;
static bool audioReady = false;
static bool audioOn = true;
static volatile uint8_t audioVol = 60;  // 0-100
static QueueHandle_t audioQ = nullptr;

static bool esW(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(ES8311_ADDR);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}
static uint8_t esR(uint8_t reg) {
  Wire.beginTransmission(ES8311_ADDR);
  Wire.write(reg);
  Wire.endTransmission(false);
  Wire.requestFrom(ES8311_ADDR, 1);
  return Wire.available() ? Wire.read() : 0;
}

static bool es8311Init() {
  Wire.beginTransmission(ES8311_ADDR);
  if (Wire.endTransmission() != 0) return false;
  esW(0x0D, 0xFA); esW(0x44, 0x08); esW(0x44, 0x08);
  esW(0x01, 0x30); esW(0x02, 0x00); esW(0x03, 0x10); esW(0x16, 0x24);
  esW(0x04, 0x10); esW(0x05, 0x00); esW(0x0B, 0x00); esW(0x0C, 0x00);
  esW(0x10, 0x1F); esW(0x11, 0x7F);
  esW(0x00, 0x80); esW(0x00, 0x80);
  esW(0x01, 0xBF);
  { uint8_t r = esR(0x06); r &= ~0x20; esW(0x06, r); }
  esW(0x13, 0x10); esW(0x1B, 0x0A); esW(0x1C, 0x6A);
  esW(0x44, 0x58);
  esW(0x02, 0x18); esW(0x05, 0x00); esW(0x03, 0x10); esW(0x04, 0x20);
  { uint8_t r = esR(0x07); r &= 0xC0; esW(0x07, r); }
  esW(0x08, 0xFF);
  { uint8_t r = esR(0x06); r &= 0xE0; r |= 0x03; esW(0x06, r); }
  esW(0x09, 0x0C); esW(0x0A, 0x0C);
  esW(0x00, 0x80); esW(0x01, 0xBF); esW(0x09, 0x0C); esW(0x0A, 0x0C);
  esW(0x17, 0xBF); esW(0x0E, 0x02); esW(0x12, 0x00); esW(0x14, 0x1A);
  esW(0x0D, 0x01); esW(0x15, 0x40); esW(0x37, 0x08); esW(0x45, 0x00);
  esW(0x32, 0xBF);
  { uint8_t r = esR(0x31); r &= 0x9F; esW(0x31, r); }
  return true;
}

struct Note { uint16_t f, ms; };
static const Note N_TAP[] = {{880, 30}};
static const Note N_ALERT[] = {{988, 90}, {0, 40}, {1319, 90}, {0, 40}, {988, 90}, {0, 40}, {1319, 140}};
static const Note N_DONE[] = {{784, 80}, {988, 80}, {1175, 80}, {1568, 220}};
static const Note N_ERROR[] = {{330, 120}, {0, 30}, {247, 220}};
static const Note N_HEART[] = {{1047, 55}, {1319, 90}};
static const Note N_LEVEL[] = {{523, 80}, {659, 80}, {784, 80}, {1047, 90}, {1319, 230}};
static const Note N_TIMER[] = {{1047, 120}, {0, 60}, {1047, 120}, {0, 60}, {1319, 120}, {0, 60}, {1568, 300}};
static const Note N_OFF[] = {{784, 110}, {659, 110}, {523, 110}, {392, 130}, {262, 320}};
static const Note N_DIZZY[] = {{600, 70}, {450, 70}, {600, 70}, {450, 70}, {300, 160}};
struct SfxDef { const Note *n; uint8_t len; };
static const SfxDef SFX[SFX_COUNT] = {
  {N_TAP, 1}, {N_ALERT, 7}, {N_DONE, 4}, {N_ERROR, 3},
  {N_HEART, 2}, {N_LEVEL, 5}, {N_TIMER, 7}, {N_DIZZY, 5}, {N_OFF, 5},
};

static int16_t sbuf[256 * 2];

static void playTone(uint16_t f, uint16_t ms) {
  int total = SAMPLE_RATE * ms / 1000;
  int half = f ? (SAMPLE_RATE / (2 * f)) : 0;
  const int16_t amp = (int16_t)(8300 * audioVol / 100);
  int phase = 0, done = 0;
  bool high = true;
  while (done < total) {
    int n = total - done;
    if (n > 256) n = 256;
    for (int i = 0; i < n; i++) {
      int16_t s = 0;
      if (f) {
        s = high ? amp : -amp;
        int idx = done + i;
        if (idx < 64) s = (int16_t)(s * idx / 64);
        else if (idx > total - 96) s = (int16_t)(s * (total - idx) / 96);
        if (++phase >= half) { phase = 0; high = !high; }
      }
      sbuf[i * 2] = s;
      sbuf[i * 2 + 1] = s;
    }
    i2s.write((uint8_t *)sbuf, n * 4);
    done += n;
  }
}

static void audioTask(void *) {
  uint8_t id;
  for (;;) {
    if (xQueueReceive(audioQ, &id, portMAX_DELAY) && audioOn && id < SFX_COUNT) {
      digitalWrite(PIN_PA, HIGH);        // l'amplificatore consuma e sibila: acceso solo mentre suona
      vTaskDelay(pdMS_TO_TICKS(60));     // l'NS4150B si stabilizza in qualche decina di ms
      const SfxDef &d = SFX[id];
      for (uint8_t i = 0; i < d.len; i++) playTone(d.n[i].f, d.n[i].ms);
      // coda: se non c'e' altro in arrivo, spegne
      vTaskDelay(pdMS_TO_TICKS(80));
      if (uxQueueMessagesWaiting(audioQ) == 0) digitalWrite(PIN_PA, LOW);
    }
  }
}

static void audioBegin() {
  pinMode(PIN_PA, OUTPUT);
  digitalWrite(PIN_PA, LOW);
  Preferences p;
  p.begin("claudebuddy", true);
  audioOn = p.getBool("snd", true);
  audioVol = p.getUChar("vol", 60);
  p.end();
  i2s.setPins(PIN_I2S_BCK, PIN_I2S_WS, PIN_I2S_DO, -1, PIN_I2S_MCK);
  if (!i2s.begin(I2S_MODE_STD, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO, I2S_STD_SLOT_BOTH)) return;
  if (!es8311Init()) return;
  audioQ = xQueueCreate(6, sizeof(uint8_t));
  xTaskCreatePinnedToCore(audioTask, "audio", 4096, nullptr, 1, nullptr, 0);
  audioReady = true;
}

static bool quiet = false;
void hwStandby(bool on) {
  quiet = on;
  if (on && audioQ) xQueueReset(audioQ);
}

void sfxPlay(uint8_t id) {
  if (quiet || audioVol == 0) return;
  if (audioReady && audioOn && audioQ) xQueueSend(audioQ, &id, 0);
}
void audioSetEnabled(bool on) {
  audioOn = on;
  Preferences p;
  p.begin("claudebuddy", false);
  p.putBool("snd", on);
  p.end();
}
bool audioEnabled() { return audioReady && audioOn; }
uint8_t audioVolume() { return audioVol; }
void audioSetVolume(uint8_t pct) {
  audioVol = pct > 100 ? 100 : pct;
  Preferences p;
  p.begin("claudebuddy", false);
  p.putUChar("vol", audioVol);
  p.end();
}

// ===================================================================== IMU
// QMI8658 a registri diretti (solo accelerometro, 4 g, ~62 Hz).
#define QMI_ADDR_L 0x6B
#define QMI_ADDR_H 0x6A
static uint8_t imuAddr = QMI_ADDR_L;
static bool imuOk = false;

static bool imuWr(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(imuAddr);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}
static bool imuRd(uint8_t reg, uint8_t *buf, uint8_t n) {
  Wire.beginTransmission(imuAddr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(imuAddr, n) != n) return false;
  for (uint8_t i = 0; i < n; i++) buf[i] = Wire.read();
  return true;
}
static bool i2cPresent(uint8_t a) {
  Wire.beginTransmission(a);
  return Wire.endTransmission() == 0;
}

static void imuBegin() {
  if (i2cPresent(QMI_ADDR_L)) imuAddr = QMI_ADDR_L;
  else if (i2cPresent(QMI_ADDR_H)) imuAddr = QMI_ADDR_H;
  else return;
  imuWr(0x60, 0xB0);  // reset
  delay(5);
  uint32_t t0 = millis();
  uint8_t v = 0;
  while ((!imuRd(0x4D, &v, 1) || !(v & 0x80)) && millis() - t0 < 500) delay(10);
  if (!imuRd(0x00, &v, 1) || v != 0x05) return;
  imuWr(0x02, 0x40);         // auto-incremento
  imuWr(0x08, 0x00);
  imuWr(0x03, (0x01 << 4) | 0x07);  // 4 g, 62.5 Hz
  imuWr(0x06, 0x00);
  imuWr(0x08, 0x01);         // accelerometro on
  imuOk = true;
}

bool imuShaken(uint32_t now) {
  static uint32_t lastPoll = 0, lastPeak = 0, cooldown = 0;
  static uint8_t peaks = 0;
  if (!imuOk || now - lastPoll < 20) return false;
  lastPoll = now;
  uint8_t b[6];
  if (!imuRd(0x35, b, 6)) return false;
  const float k = 4.0f / 32768.0f;
  float x = (int16_t)(b[0] | (b[1] << 8)) * k;
  float y = (int16_t)(b[2] | (b[3] << 8)) * k;
  float z = (int16_t)(b[4] | (b[5] << 8)) * k;
  float dyn = fabsf(sqrtf(x * x + y * y + z * z) - 1.0f);
  if ((int32_t)(now - lastPeak) > 700) peaks = 0;
  if (dyn > 1.0f && (int32_t)(now - lastPeak) > 90) {
    lastPeak = now;
    if (++peaks >= 4 && (int32_t)(now - cooldown) >= 0) {
      cooldown = now + 4000;
      peaks = 0;
      return true;
    }
  }
  return false;
}

// ===================================================================== RTC
static SensorPCF85063 rtc;
static bool rtcOk = false;

bool rtcPresent() { return rtcOk; }

uint32_t rtcGet() {
  if (!rtcOk) return 0;
  RTC_DateTime t = rtc.getDateTime();
  if (t.getYear() < 2025 || t.getYear() > 2120) return 0;
  struct tm tmv = {};
  tmv.tm_year = t.getYear() - 1900;
  tmv.tm_mon = t.getMonth() - 1;
  tmv.tm_mday = t.getDay();
  tmv.tm_hour = t.getHour();
  tmv.tm_min = t.getMinute();
  tmv.tm_sec = t.getSecond();
  time_t e = mktime(&tmv);  // TZ di default = UTC
  return e > 0 ? (uint32_t)e : 0;
}

void rtcSet(uint32_t e) {
  if (!rtcOk) return;
  time_t tt = e;
  struct tm tmv;
  gmtime_r(&tt, &tmv);
  rtc.setDateTime(RTC_DateTime(tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday, tmv.tm_hour, tmv.tm_min, tmv.tm_sec));
}

// ===================================================================== batteria
static XPowersPMU pmu;
static bool pmuOk = false;
static int cPct = -1;
static bool cCharging = false, cUsb = true;

static void batBegin() {
  pmuOk = pmu.begin(Wire, AXP2101_SLAVE_ADDRESS, IIC_SDA, IIC_SCL);
  if (!pmuOk) return;
  pmu.enableBattVoltageMeasure();
  // tasto POWER: pressione breve e lunga (1.5 s) come eventi, letti a polling (niente pin IRQ).
  // Lo spegnimento hardware con pressione di 6 s resta come ultima risorsa.
  pmu.disableIRQ(XPOWERS_AXP2101_ALL_IRQ);
  pmu.clearIrqStatus();
  pmu.setIrqLevelTime(XPOWERS_AXP2101_IRQ_TIME_1S5);
  pmu.setPowerKeyPressOffTime(XPOWERS_POWEROFF_6S);
  pmu.enableIRQ(XPOWERS_AXP2101_PKEY_SHORT_IRQ | XPOWERS_AXP2101_PKEY_LONG_IRQ);
}

PwrKey pwrKeyPoll(uint32_t now) {
  static uint32_t last = 0;
  if (!pmuOk || now - last < 100) return PWR_NONE;
  last = now;
  pmu.getIrqStatus();
  PwrKey k = PWR_NONE;
  if (pmu.isPekeyLongPressIrq()) k = PWR_LONG;
  else if (pmu.isPekeyShortPressIrq()) k = PWR_SHORT;
  if (k != PWR_NONE) pmu.clearIrqStatus();
  return k;
}

void hwPowerOff() {
  if (pmuOk) pmu.shutdown();
}

void batUpdate(uint32_t now) {
  static uint32_t last = 0;
  if (!pmuOk || (last && now - last < 5000)) return;
  last = now ? now : 1;
  bool conn = pmu.isBatteryConnect();
  cCharging = pmu.isCharging();
  cUsb = pmu.isVbusIn();
  cPct = conn ? pmu.getBatteryPercent() : -1;
}
int batPercent() { return cPct; }
bool batCharging() { return cCharging; }
bool usbPresent() { return cUsb; }

// ===================================================================== init
void hwBegin() {
  audioBegin();
  imuBegin();
  rtcOk = rtc.begin(Wire, IIC_SDA, IIC_SCL);
  batBegin();
}

bool hwAudioOk() { return audioReady; }
bool hwImuOk() { return imuOk; }
bool hwPmuOk() { return pmuOk; }
