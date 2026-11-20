// SPDX-License-Identifier: MIT
#pragma once
// Periferiche della scheda: audio (ES8311), movimento (QMI8658), RTC (PCF85063), batteria (AXP2101).
// Tutte opzionali: se un chip non risponde, le funzioni non fanno nulla.
// Chiamare hwBegin() dopo Wire.begin().
#include <stdint.h>

enum Sfx : uint8_t {
  SFX_TAP = 0,   // tocco / cambio pagina
  SFX_ALERT,     // serve la tua attenzione
  SFX_DONE,      // task finito
  SFX_ERROR,     // un tool e' fallito
  SFX_HEART,     // carezza
  SFX_LEVEL,     // Clawd sale di livello
  SFX_TIMER,     // timer finito
  SFX_DIZZY,     // scossa
  SFX_COUNT
};

void hwBegin();
bool hwAudioOk();   // stato dei chip, per la pagina Info
bool hwImuOk();
bool hwPmuOk();

// audio (non bloccante: gli effetti vanno in coda a una task dedicata)
void sfxPlay(uint8_t id);
void audioSetEnabled(bool on);  // salvato in flash
bool audioEnabled();

// movimento: true quando la scheda viene scossa con forza (max una volta ogni 4 s)
bool imuShaken(uint32_t nowMs);

// RTC: ora "a muro" locale come secondi dal 1970 (0 = ora non valida)
bool rtcPresent();
uint32_t rtcGet();
void rtcSet(uint32_t localEpoch);

// batteria
void batUpdate(uint32_t nowMs);  // legge il PMU ogni qualche secondo
int batPercent();                // -1 se non c'e' batteria
bool batCharging();
bool usbPresent();
