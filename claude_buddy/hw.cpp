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
#define PIN_I2S_DI 10  // uscita dati dell'ES7210 (microfoni)
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

// ---- Mini sintetizzatore ----
// Ogni nota ha: frequenza (f), frequenza finale per il glissando (f2, 0 = nessuno), seconda voce per
// gli accordi (fb, 0 = nessuna), durata, forma d'onda e ampiezza relativa (0-100).
//   SINE  = morbida, TRI = "triste"/vuota, SOFTSQ = quadra addolcita (allarmi),
//   BELL  = timbro a campanella con decadimento naturale (il suono di base del buddy).
enum Wave : uint8_t { WV_SINE, WV_TRI, WV_BELL, WV_SOFTSQ };
// ---- ES7210: ADC dei due microfoni (l'ES8311 serve solo per l'altoparlante) ----
// Sequenza ricavata dal driver ES7210 di ESPHome (MIT/Apache, derivato da Espressif):
// slave I2S 16 bit non-TDM (MIC1 = canale sinistro, MIC2 = destro), MCLK = 256 * fs = 4,096 MHz.
#define ES7210_ADDR 0x40
#define MIC_GAIN_CODE 10  // 30 dB (codice = dB / 3)
static bool es7210Ok = false;

static bool e7W(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(ES7210_ADDR);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}
// aggiorna solo i bit di mask
static bool e7Upd(uint8_t reg, uint8_t mask, uint8_t val) {
  Wire.beginTransmission(ES7210_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)ES7210_ADDR, (uint8_t)1) != 1) return false;
  uint8_t v = Wire.read();
  return e7W(reg, (v & ~mask) | (val & mask));
}

static bool es7210Init() {
  Wire.beginTransmission(ES7210_ADDR);
  if (Wire.endTransmission() != 0) return false;
  static const uint8_t RST[][2] = {
    {0x00, 0xFF}, {0x00, 0x32}, {0x01, 0x3F},  // reset software, clock spenti
    {0x09, 0x30}, {0x0A, 0x30},                // tempi di accensione
    {0x23, 0x2A}, {0x22, 0x0A}, {0x20, 0x0A}, {0x21, 0x2A},  // filtri passa-alto
  };
  for (auto &r : RST) {
    if (!e7W(r[0], r[1])) return false;
    if (r[0] == 0x00 && r[1] == 0xFF) delay(5);
  }
  bool ok = e7Upd(0x08, 0x01, 0x00);  // slave: il clock lo da' l'ESP32
  ok &= e7W(0x40, 0xC3) && e7W(0x41, 0x70) && e7W(0x42, 0x70);  // alimentazione analogica e bias
  ok &= e7W(0x11, 0x60) && e7W(0x12, 0x00);                      // 16 bit, MIC1/2 su SDOUT1
  ok &= e7W(0x02, 0xC1) && e7W(0x07, 0x20) && e7W(0x04, 0x01) && e7W(0x05, 0x00);  // 16 kHz con MCLK 4,096 MHz
  // guadagno e accensione dei quattro ingressi (come nel driver originale)
  for (uint8_t i = 0; i < 4; i++) ok &= e7Upd(0x43 + i, 0x10, 0x00);
  ok &= e7W(0x4B, 0xFF) && e7W(0x4C, 0xFF);
  for (uint8_t m = 0; m < 4; m++) {
    const bool lo = m < 2;  // MIC1/2 usano i clock 0x0B, MIC3/4 i clock 0x15
    ok &= e7Upd(0x01, lo ? 0x0B : 0x15, 0x00);
    ok &= e7W(lo ? 0x4B : 0x4C, 0x00);
    ok &= e7Upd(0x43 + m, 0x10, 0x10);
    ok &= e7Upd(0x43 + m, 0x0F, MIC_GAIN_CODE);
  }
  ok &= e7W(0x47, 0x08) && e7W(0x48, 0x08) && e7W(0x49, 0x08) && e7W(0x4A, 0x08);
  ok &= e7W(0x06, 0x04);
  ok &= e7W(0x4B, 0x0F) && e7W(0x4C, 0x0F);
  ok &= e7W(0x00, 0x71) && e7W(0x00, 0x41);  // abilita
  return ok;
}

struct Note { uint16_t f, f2, fb, ms; uint8_t wave, amp; };
#define SY_NOTE(f, ms, w, a) { f, 0, 0, ms, w, a }
#define SY_GLIDE(f, f2, ms, w, a) { f, f2, 0, ms, w, a }
#define SY_CHORD(f, fb, ms, w, a) { f, 0, fb, ms, w, a }
#define SY_REST(ms) { 0, 0, 0, ms, WV_SINE, 0 }

static const Note N_TAP[] = { SY_NOTE(1568, 28, WV_BELL, 60) };
static const Note N_ALERT[] = {
  SY_CHORD(988, 1480, 110, WV_BELL, 100), SY_REST(45), SY_CHORD(1319, 1976, 110, WV_BELL, 100), SY_REST(45),
  SY_CHORD(988, 1480, 110, WV_BELL, 100), SY_REST(45), SY_CHORD(1319, 1976, 280, WV_BELL, 100) };
static const Note N_DONE[] = {
  SY_NOTE(784, 75, WV_BELL, 90), SY_NOTE(988, 75, WV_BELL, 90), SY_NOTE(1175, 75, WV_BELL, 90),
  SY_CHORD(1568, 1976, 340, WV_BELL, 100) };
static const Note N_ERROR[] = {
  SY_GLIDE(440, 330, 130, WV_TRI, 85), SY_REST(25), SY_GLIDE(349, 247, 290, WV_TRI, 85) };
static const Note N_HEART[] = { SY_NOTE(1047, 55, WV_BELL, 75), SY_NOTE(1319, 130, WV_BELL, 75) };
static const Note N_LEVEL[] = {
  SY_NOTE(523, 75, WV_BELL, 90), SY_NOTE(659, 75, WV_BELL, 90), SY_NOTE(784, 75, WV_BELL, 90),
  SY_NOTE(1047, 85, WV_BELL, 90), SY_REST(20), SY_CHORD(1319, 1568, 100, WV_BELL, 100),
  SY_CHORD(1047, 1568, 460, WV_BELL, 100) };
static const Note N_TIMER[] = {
  SY_NOTE(1319, 140, WV_BELL, 100), SY_REST(60), SY_NOTE(1319, 140, WV_BELL, 100), SY_REST(60),
  SY_NOTE(1319, 140, WV_BELL, 100), SY_REST(60), SY_CHORD(1568, 2093, 360, WV_BELL, 100) };
static const Note N_OFF[] = {
  SY_NOTE(784, 110, WV_SINE, 80), SY_NOTE(659, 110, WV_SINE, 80), SY_NOTE(523, 110, WV_SINE, 80),
  SY_NOTE(392, 130, WV_SINE, 80), SY_GLIDE(392, 262, 420, WV_SINE, 70) };
static const Note N_DIZZY[] = {
  SY_GLIDE(600, 300, 110, WV_SINE, 80), SY_GLIDE(300, 600, 110, WV_SINE, 80), SY_GLIDE(600, 300, 110, WV_SINE, 80),
  SY_GLIDE(300, 600, 110, WV_SINE, 80), SY_GLIDE(500, 200, 230, WV_SINE, 80) };
static const Note N_BOOT[] = {
  SY_GLIDE(392, 784, 130, WV_SINE, 65), SY_NOTE(1047, 70, WV_BELL, 90), SY_NOTE(1319, 70, WV_BELL, 90),
  SY_NOTE(1568, 70, WV_BELL, 90), SY_CHORD(1047, 2093, 420, WV_BELL, 100) };
static const Note N_COMMIT[] = { SY_NOTE(1319, 55, WV_BELL, 85), SY_NOTE(1760, 140, WV_BELL, 90) };
static const Note N_PUSH[] = {
  SY_GLIDE(440, 1760, 230, WV_SINE, 60), SY_NOTE(1568, 60, WV_BELL, 85), SY_CHORD(2093, 1047, 320, WV_BELL, 100) };
static const Note N_TESTOK[] = {
  SY_NOTE(1047, 70, WV_BELL, 90), SY_NOTE(1319, 70, WV_BELL, 90), SY_CHORD(1568, 2093, 280, WV_BELL, 100) };
static const Note N_COMPACT[] = { SY_GLIDE(1568, 392, 260, WV_SINE, 65), SY_REST(20), SY_NOTE(1760, 45, WV_BELL, 60) };
static const Note N_ALARM[] = {
  SY_NOTE(1319, 100, WV_SOFTSQ, 80), SY_REST(40), SY_NOTE(988, 100, WV_SOFTSQ, 80), SY_REST(40),
  SY_NOTE(1319, 100, WV_SOFTSQ, 80), SY_REST(40), SY_NOTE(988, 240, WV_SOFTSQ, 80) };
static const Note N_RESET[] = {
  SY_GLIDE(523, 1047, 150, WV_SINE, 60), SY_NOTE(1319, 80, WV_BELL, 90), SY_CHORD(1568, 2093, 380, WV_BELL, 100) };

struct SfxDef { const Note *n; uint8_t len; };
#define SY_DEF(a) { a, (uint8_t)(sizeof(a) / sizeof(a[0])) }
static const SfxDef SFX[SFX_COUNT] = {  // stesso ordine di enum Sfx
  SY_DEF(N_TAP), SY_DEF(N_ALERT), SY_DEF(N_DONE), SY_DEF(N_ERROR), SY_DEF(N_HEART), SY_DEF(N_LEVEL),
  SY_DEF(N_TIMER), SY_DEF(N_DIZZY), SY_DEF(N_OFF), SY_DEF(N_BOOT), SY_DEF(N_COMMIT), SY_DEF(N_PUSH),
  SY_DEF(N_TESTOK), SY_DEF(N_COMPACT), SY_DEF(N_ALARM), SY_DEF(N_RESET),
};

static int16_t sinTab[256];
static int16_t sbuf[256 * 2];
static bool quiet = false;
static volatile bool autoVolOn = false;
static volatile float autoK = 1.0f;  // fattore del volume automatico (0.45 stanza silenziosa ... 1.2 rumorosa)

// volume realmente usato, 0-1
static float effVolume() {
  float v = audioVol * (autoVolOn ? autoK : 1.0f);
  if (v > 100.0f) v = 100.0f;
  return v / 100.0f;
}

// un campione dell'oscillatore; ph = fase a 32 bit
static inline int32_t osc(uint8_t wave, uint32_t ph) {
  const uint8_t i = ph >> 24;
  switch (wave) {
    case WV_TRI: {
      int32_t p = ph >> 16;
      return (p < 32768 ? p : 65535 - p) * 2 - 32768;
    }
    case WV_BELL:  // fondamentale + 2a + 4a armonica
      return ((int32_t)sinTab[i] * 10 + (int32_t)sinTab[(uint8_t)(i * 2)] * 4 + (int32_t)sinTab[(uint8_t)(i * 4)] * 2) / 16;
    case WV_SOFTSQ:  // quadra addolcita: fondamentale + 3a + 5a armonica
      return ((int32_t)sinTab[i] * 12 + (int32_t)sinTab[(uint8_t)(i * 3)] * 4 + (int32_t)sinTab[(uint8_t)(i * 5)] * 2) / 16;
    default:
      return sinTab[i];
  }
}

static void playNote(const Note &n) {
  const int total = SAMPLE_RATE * n.ms / 1000;
  if (total <= 0) return;
  const int32_t peak = (int32_t)(12000.0f * effVolume() * n.amp / 100.0f);
  const bool two = n.fb != 0;
  const bool bell = n.wave == WV_BELL;  // decadimento naturale su tutta la durata
  const int att = total / 4 < 96 ? total / 4 : 96;            // attacco ~6 ms: niente click
  const int rel = total / 3 < 800 ? total / 3 : 800;          // rilascio delle note tenute
  const float k = 4294967296.0f / SAMPLE_RATE;                 // incremento di fase per Hz
  uint32_t ph = 0, phb = 0;
  int done = 0;
  while (done < total) {
    int c = total - done;
    if (c > 256) c = 256;
    for (int i = 0; i < c; i++) {
      const int idx = done + i;
      int32_t s = 0;
      if (n.f && peak) {
        float fr = n.f2 ? n.f + (float)(n.f2 - n.f) * idx / total : (float)n.f;
        ph += (uint32_t)(fr * k);
        s = osc(n.wave, ph);
        if (two) {
          phb += (uint32_t)((n.f ? fr * n.fb / n.f : (float)n.fb) * k);
          s = (s + osc(n.wave, phb)) * 5 / 8;
        }
        int32_t e = 1024;  // inviluppo 0..1024
        if (idx < att) e = e * idx / att;
        if (bell) {
          int32_t q = (int32_t)(total - idx) * 1024 / total;
          e = e * q / 1024 * q / 1024;
        } else if (total - idx < rel) {
          e = e * (total - idx) / rel;
        }
        s = (((s * peak) >> 15) * e) >> 10;
      }
      sbuf[i * 2] = (int16_t)s;
      sbuf[i * 2 + 1] = (int16_t)s;
    }
    i2s.write((uint8_t *)sbuf, c * 4);
    done += c;
  }
}

// ---- Microfono: misura del rumore ambiente (ES7210, dato sul pin DIN) ----
static volatile int micDb = -1;
static bool micDebug = false;
static float ambDb = -1;
static uint8_t micDeadCount = 0;
static uint32_t micRetryAt = 0;  // dopo 3 misure fallite si smette di provare per 5 minuti

static void micMeasure() {
  const uint32_t t0 = millis();
  // il DMA contiene dati vecchi: si scartano, poi si misurano ~130 ms
  for (int i = 0; i < 6 && millis() - t0 < 400; i++) i2s.readBytes((char *)sbuf, sizeof(sbuf));
  const int FR = 256, CH = 8;
  double sum = 0, sum2 = 0;
  int cnt = 0;
  for (int b = 0; b < CH && millis() - t0 < 700; b++) {
    size_t got = i2s.readBytes((char *)sbuf, FR * 4) / 4;
    for (size_t i = 0; i < got; i++) {
      int32_t l = sbuf[i * 2], r = sbuf[i * 2 + 1];
      int32_t v = abs(l) >= abs(r) ? l : r;  // il codec puo' mettere il dato su uno slot solo
      sum += v;
      sum2 += (double)v * v;
      cnt++;
    }
  }
  const double mean = cnt ? sum / cnt : 0;
  const float rms = cnt ? sqrtf((float)fmax(sum2 / cnt - mean * mean, 0.0)) : 0.0f;
  if (micDebug) Serial.printf("MIC samples=%d rms=%.1f amb=%.1fdB k=%.2f\n", cnt, rms, ambDb, (double)autoK);
  if (cnt < FR || rms < 0.6f) {  // nessun dato o silenzio digitale assoluto: microfono assente o non collegato
    if (++micDeadCount >= 3) {
      micDb = -1;
      ambDb = -1;
      autoK = 1.0f;
      micRetryAt = millis() + 300000UL;
    }
    return;
  }
  micDeadCount = 0;
  const float db = 20.0f * log10f(rms + 1.0f);
  if (ambDb < 0) ambDb = db;
  // sale piano coi rumori improvvisi (applausi, voce), scende subito quando torna la calma
  else ambDb += (db - ambDb) * (db < ambDb + 10.0f ? 0.3f : 0.05f);
  micDb = (int)(ambDb + 0.5f);
  // 40 dB (stanza silenziosa) -> 45% del volume; 66 dB (rumore) -> 120%
  float target = 0.45f + 0.75f * (ambDb - 40.0f) / 26.0f;
  if (target < 0.45f) target = 0.45f;
  if (target > 1.2f) target = 1.2f;
  autoK += (target - autoK) * 0.5f;
}

static void audioTask(void *) {
  uint8_t id;
  uint32_t lastSound = 0;
  for (;;) {
    if (xQueueReceive(audioQ, &id, pdMS_TO_TICKS(2000))) {
      if (audioOn && id < SFX_COUNT) {
        digitalWrite(PIN_PA, HIGH);        // l'amplificatore consuma e sibila: acceso solo mentre suona
        vTaskDelay(pdMS_TO_TICKS(60));     // l'NS4150B si stabilizza in qualche decina di ms
        const SfxDef &d = SFX[id];
        for (uint8_t i = 0; i < d.len; i++) playNote(d.n[i]);
        // coda: se non c'e' altro in arrivo, spegne
        vTaskDelay(pdMS_TO_TICKS(80));
        if (uxQueueMessagesWaiting(audioQ) == 0) digitalWrite(PIN_PA, LOW);
        lastSound = millis();
      }
    } else if (autoVolOn && es7210Ok && audioOn && !quiet && millis() - lastSound > 1000 && (int32_t)(millis() - micRetryAt) >= 0) {
      micMeasure();  // coda vuota da 2 s e altoparlante zitto: si ascolta la stanza
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
  autoVolOn = p.getBool("avol", false);
  p.end();
  for (int i = 0; i < 256; i++) sinTab[i] = (int16_t)lroundf(sinf(i * 6.2831853f / 256.0f) * 32767.0f);
  i2s.setPins(PIN_I2S_BCK, PIN_I2S_WS, PIN_I2S_DO, PIN_I2S_DI, PIN_I2S_MCK);
  if (!i2s.begin(I2S_MODE_STD, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO, I2S_STD_SLOT_BOTH)) return;
  if (!es8311Init()) return;
  es7210Ok = es7210Init();  // senza, il volume automatico resta inattivo ma l'audio funziona
  audioQ = xQueueCreate(6, sizeof(uint8_t));
  xTaskCreatePinnedToCore(audioTask, "audio", 4096, nullptr, 1, nullptr, 0);
  audioReady = true;
}

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
bool audioAutoVol() { return autoVolOn; }
void audioSetAutoVol(bool on) {
  autoVolOn = on;
  autoK = 1.0f;
  ambDb = -1;
  micDb = -1;
  micDeadCount = 0;
  micRetryAt = 0;
  Preferences p;
  p.begin("claudebuddy", false);
  p.putBool("avol", on);
  p.end();
}
int audioMicDb() { return autoVolOn ? micDb : -1; }
uint8_t audioEffectiveVolume() { return (uint8_t)(effVolume() * 100.0f + 0.5f); }
void audioMicDebug(bool on) { micDebug = on; }
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

static float imuGx = 0, imuGy = 0;  // ultimo campione (g), per l'orientamento
static uint32_t imuSeq = 0;         // incrementa a ogni campione nuovo

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
  imuGx = x;
  imuGy = y;
  imuSeq++;
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

// Orientamento: quale bordo dello schermo guarda in su (0 alto, 1 destro, 2 basso, 3 sinistro),
// che e' anche il valore per setRotation(). Si basa sui campioni di imuShaken() (chiamarla spesso).
// Filtro passa-basso, isteresi e 0,4 s di stabilita': non salta mentre si prende in mano il buddy.
// Con la scheda distesa (gravita' quasi tutta su Z) mantiene l'orientamento corrente.
static uint8_t oriCur = 0;
static bool oriDebug = false;
static bool oriFresh = true;  // dopo l'attivazione: la prima misura valida si applica subito, senza attesa

void imuOrientationReset() {
  oriCur = 0;
  oriFresh = true;
}
void imuOrientDebug(bool on) { oriDebug = on; }

int imuOrientation(uint32_t now) {
  static const uint8_t AX[4] = IMU_EDGE_AXIS;
  static const int8_t SG[4] = IMU_EDGE_SIGN;
  static float fx = 0, fy = 0;
  static uint32_t lastSeq = 0, candSince = 0, lastDbg = 0;
  static uint8_t cand = 0;
  if (!imuOk || imuSeq == 0) return -1;
  if (oriFresh) {  // filtro riallineato al campione corrente
    fx = imuGx;
    fy = imuGy;
    lastSeq = imuSeq;
  } else if (imuSeq != lastSeq) {
    lastSeq = imuSeq;
    fx += (imuGx - fx) * 0.2f;
    fy += (imuGy - fy) * 0.2f;
  }
  float v[4];  // quanto ogni bordo "guarda in su"
  for (int e = 0; e < 4; e++) v[e] = (AX[e] ? fy : fx) * SG[e];
  uint8_t best = 0;
  for (uint8_t e = 1; e < 4; e++)
    if (v[e] > v[best]) best = e;
  if (oriFresh) {
    if (v[best] < 0.6f) return 0;  // scheda distesa: nessuna rotazione finche' non si capisce
    oriCur = cand = best;
    oriFresh = false;
  }
  uint8_t want = oriCur;
  if (best != oriCur && v[best] > 0.6f && v[best] > v[oriCur] + 0.4f) want = best;
  if (want != oriCur) {
    if (want != cand) { cand = want; candSince = now; }
    else if (now - candSince > 400) oriCur = want;
  } else {
    cand = oriCur;
  }
  if (oriDebug && now - lastDbg > 500) {
    lastDbg = now;
    Serial.printf("ORI gx=%.2f gy=%.2f up-edge=%d rot=%d\n", (double)fx, (double)fy, (int)best, (int)((oriCur + IMU_ROT_SHIFT) & 3));
  }
  return (oriCur + IMU_ROT_SHIFT) & 3;
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
