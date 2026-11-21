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
  SFX_OFF,       // spegnimento
  SFX_BOOT,      // accensione
  SFX_COMMIT,    // commit riuscito
  SFX_PUSH,      // push riuscito
  SFX_TESTOK,    // test passati
  SFX_COMPACT,   // compattazione del contesto
  SFX_ALARM,     // usage oltre la soglia
  SFX_RESET,     // limite azzerato
  SFX_COUNT
};

void hwBegin();
bool hwAudioOk();   // stato dei chip, per la pagina Info
bool hwImuOk();
bool hwPmuOk();

// audio (non bloccante: gli effetti vanno in coda a una task dedicata)
void sfxPlay(uint8_t id);
void audioSetEnabled(bool on);  // salvato in flash
void audioSetVolume(uint8_t pct);  // 0-100, salvato in flash (0 = muto)
uint8_t audioVolume();
bool audioEnabled();

// volume automatico: il microfono misura il rumore della stanza e adatta il volume
// (poco volume in una stanza silenziosa, di piu' se c'e' rumore). Salvato in flash.
void audioSetAutoVol(bool on);
bool audioAutoVol();
int audioMicDb();               // rumore ambiente in dB (scala digitale), -1 = nessuna misura valida
uint8_t audioEffectiveVolume(); // volume realmente usato (volume * fattore automatico), 0-100
void audioMicDebug(bool on);    // stampa le misure sulla seriale (per tarare le soglie)

// movimento: true quando la scheda viene scossa con forza (max una volta ogni 4 s)
bool imuShaken(uint32_t nowMs);

// orientamento: bordo dello schermo che guarda in su (0 alto, 1 destro, 2 basso, 3 sinistro),
// cioe' il valore da dare a setRotation(). -1 se l'IMU manca. Chiamare spesso, insieme a imuShaken().
int imuOrientation(uint32_t nowMs);
void imuOrientationReset();     // torna a 0 (quando la funzione viene disattivata)
void imuOrientDebug(bool on);   // stampa gx/gy e la rotazione scelta sulla seriale (per tarare board.h)

// RTC: ora "a muro" locale come secondi dal 1970 (0 = ora non valida)
bool rtcPresent();
uint32_t rtcGet();
void rtcSet(uint32_t localEpoch);

// batteria
void batUpdate(uint32_t nowMs);  // legge il PMU ogni qualche secondo
int batPercent();                // -1 se non c'e' batteria
bool batCharging();
bool usbPresent();

// tasto POWER (collegato al PMU AXP2101): chiamare spesso (si limita da solo a ~10 Hz)
enum PwrKey : uint8_t { PWR_NONE = 0, PWR_SHORT, PWR_LONG };
PwrKey pwrKeyPoll(uint32_t nowMs);
void hwPowerOff();            // spegne le tensioni (a batteria); con USB collegato potrebbe non spegnersi
void hwStandby(bool on);      // standby: zittisce l'audio e ferma i suoni in coda
