// SPDX-License-Identifier: MIT
/*
  Claude Buddy - compagno da scrivania per Claude Code
  Scheda: Waveshare ESP32-S3-Touch-AMOLED-1.75 / 1.75-B / 1.75C (CO5300 466x466)

  Mostra Clawd, la mascotte di Claude Code, che reagisce a cio' che Claude sta facendo.
  Riceve righe di testo via USB seriale oppure via BLE (Nordic UART, una per riga) dallo script
  host/buddy.py, che a sua volta e' chiamato dagli hook e dallo statusLine di Claude Code.

  Protocollo (maiuscole/minuscole indifferenti): idle | thinking | working [tool] |
  attention [msg] | done | error | sleep, sessions N, sess ..., usage ..., agents N, pet xp,
  stats ..., today ..., todo ..., msg ..., tick ..., fx ..., epoch ..., time HH:MM,
  bright N, sound 0|1, standby, wake, off, autostandby MIN, unpair. Il dettaglio di ogni comando e' in README.md e in host/buddy.py.

  Interfaccia: swipe orizzontale tra le pagine (Clawd, Sessioni, Usage, Oggi, Timer,
  Messaggio, Stile, Info); tieni premuto su Clawd per aprire Stile.
  Tasto BOOT (GPIO0): breve = scorre gli stati (prova senza PC); tenuto premuto = impostazioni.

  Impostazioni Arduino IDE (vedi README.md):
    USB CDC On Boot: Enabled, PSRAM: OPI PSRAM, Flash 16MB,
    Partition scheme: app3M_fat9M_16MB
*/

#include <Arduino.h>
#include <Wire.h>
#include <math.h>
#include <time.h>
#include <sys/time.h>
#include <Preferences.h>
#include "Arduino_GFX_Library.h"
#include "board.h"
#include "hw.h"
#include "ble.h"
#include "TouchDrv.hpp"  // SensorLib: touch CST9217

// ---- Pin (da HARDWARE_REFERENCE.md di Waveshare) ----
#define LCD_SDIO0 4
#define LCD_SDIO1 5
#define LCD_SDIO2 6
#define LCD_SDIO3 7
#define LCD_SCLK 38
#define LCD_CS 12
#define LCD_RESET LCD_RESET_PIN
#define LCD_WIDTH 466
#define LCD_HEIGHT 466
#define BOOT_PIN 0
// Touch CST9217 su I2C (bus condiviso con PMU, codec, IMU)
#define IIC_SDA 15
#define IIC_SCL 14
#define TP_INT 11
#define TP_RESET TP_RESET_PIN
#define TOUCH_ADDR 0x5A


// ---- Display ----
Arduino_DataBus *bus = new Arduino_ESP32QSPI(
  LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);

Arduino_CO5300 *panel = new Arduino_CO5300(
  bus, LCD_RESET, 0 /* rotation */, LCD_WIDTH, LCD_HEIGHT, 6, 0, 0, 0);

// Canvas in PSRAM: disegno fuori schermo e poi un solo flush (niente sfarfallio).
// Se la PSRAM non e' attiva si ripiega sul disegno diretto.
Arduino_Canvas *canvas = nullptr;
Arduino_GFX *g = nullptr;  // dove si disegna davvero

const int CX = LCD_WIDTH / 2;
const int CY = LCD_HEIGHT / 2;

// ---- Stati ----
enum State { S_SLEEP, S_IDLE, S_THINKING, S_WORKING, S_ATTENTION, S_DONE, S_ERROR };
State state = S_IDLE;
char detail[28] = "";
uint32_t stateSince = 0;
uint32_t lastEvent = 0;

uint8_t userBrightness = 120;
uint8_t curBrightness = 0;

enum EyeMode { EYE_OPEN, EYE_HAPPY, EYE_CLOSED, EYE_X };

struct Pose {
  float dx = 0, dy = 0;            // spostamento (dy < 0 = in alto)
  float armL = 0, armR = 0;        // -1 giu' ... 0 riposo ... 1 braccio alzato
  float lookX = 0, lookY = 0;      // sguardo in pixel
  float openK = 1.0f;              // apertura occhi
  EyeMode eye = EYE_OPEN;
  int squash = 0;                  // quanto e' "accucciato" (px)
  float legAmp = 0, legPhase = 0;  // passetti
  bool keyboard = false;           // tastiera davanti (stato working)
  int brow = 0;                    // 1 = sopracciglia arrabbiate
  int puff = 0;                    // corpo "gonfio" (px per lato)
};

char clockText[6] = "";  // "HH:MM" oppure vuoto

TouchDrvCST92xx touch;
volatile bool gTouchIrq = false;
bool touchDebug = false;  // comando 'touchdebug 1': stampa i punti del touch sulla seriale
bool touchOk = false;

// ---- Dati dal PC: sessioni di Claude Code e usage ----
const int MAX_SESS = 5;
struct Sess {
  char st;          // I idle, T thinking, W working, A attention, D done
  int8_t ctx;       // % contesto, -1 = ignoto
  char name[18];    // cartella di lavoro
  char tool[28];    // tool in uso / messaggio
  int32_t cacheS;   // secondi di cache prompt rimasti a cacheAt (0 = fredda, -1 = ignoto)
  uint32_t cacheAt;
  char model[11];
  char effort[9];
  bool fast;
};
Sess sess[MAX_SESS];
uint8_t sessN = 0;

// usage: percentuali (-1 = ignoto) e minuti mancanti al reset, misurati a usageAt
int u5 = -1, u5min = -1, u7 = -1, u7min = -1, uctx = -1;
uint32_t usageAt = 0;

// ---- Pagine (swipe orizzontale) ----
enum Page { PG_CLAWD, PG_SESSIONS, PG_USAGE, PG_TODAY, PG_TIMER, PG_MSG, PG_STYLE, PG_INFO, PG_COUNT };
uint8_t page = PG_CLAWD;
uint32_t petUntil = 0;     // carezza: reazione di Clawd fino a questo millis
uint32_t lastTouch = 0;    // ultimo tocco (sveglia lo schermo in sleep)
// millisecondi trascorsi da t; 0 se t e' nel (piccolo) futuro rispetto a now (evita il wrap senza segno)
#define ago(now, t) (((int32_t)((now) - (t)) < 0) ? 0u : (uint32_t)((now) - (t)))

// sub-agent attivi (mini Clawd), esperienza/livello, costi di oggi
uint8_t agentsN = 0;
int petLevel = 1;
float petProg = 0;         // 0..1 verso il livello successivo
bool petKnown = false;     // dopo il primo dato dal PC (per non festeggiare all'avvio)
uint32_t lvlUpUntil = 0;
int statCents = 0, statAdd = 0, statRm = 0;

// reazioni
uint32_t dizzyUntil = 0;   // scossa: Clawd gira la testa
char toast[24] = "";       // messaggio breve al centro
uint32_t toastUntil = 0;

// orologio: sistema (impostato dal PC o letto dall'RTC)
bool clockValid = false;

// timer pomodoro (pagina Timer)
const int32_t TM_FOCUS_MS = 25L * 60000L;
const int32_t TM_BREAK_MS = 5L * 60000L;
bool tmFocus = true, tmRunning = false;
int32_t tmLeftMs = TM_FOCUS_MS;
uint32_t tmLast = 0;

// avanzamento della lista attivita' (TodoWrite)
int todoDone = 0, todoTotal = 0;

// ultimo messaggio di Claude (pagina Messaggio, si scorre in verticale)
char msgName[18] = "";
char msgText[400] = "";
uint32_t msgAgeS = 0, msgRecvAt = 0;
bool msgUnread = false;
int msgScroll = 0;

// reazioni extra
uint32_t laughUntil = 0, annoyUntil = 0, midnightUntil = 0;
uint8_t tapCount = 0;
uint32_t lastTapAt = 0;

// personalizzazione (sbloccata col livello), salvata in flash
struct ColorDef { const char *name; uint16_t c; uint8_t lvl; };
const ColorDef COLORS[] = {
  { "Arancio", RGB565(215, 119, 87), 1 }, { "Blu", RGB565(90, 140, 220), 2 },
  { "Verde", RGB565(90, 185, 120), 4 },   { "Viola", RGB565(160, 110, 210), 5 },
  { "Rosa", RGB565(230, 120, 160), 7 },   { "Oro", RGB565(230, 185, 70), 12 },
};
const int N_COLORS = sizeof(COLORS) / sizeof(COLORS[0]);
const char *const HAT_NAMES[] = { "Nessuno", "Cappellino", "Corona", "Auto" };
const uint8_t HAT_LVL[] = { 1, 3, 10, 1 };
const char *const GLS_NAMES[] = { "No", "Si", "Auto" };
const uint8_t GLS_LVL[] = { 1, 6, 1 };
uint8_t styleColor = 0, styleHat = 3, styleGlasses = 2;

#define FW_VERSION "1.1"
char ticker[124] = "";               // riga scorrevole in basso
uint32_t alarmUntil = 0, resetUntil = 0;  // allarme usage / limite azzerato
int alarmPct = 0;
// ---- Ingresso righe: seriale USB e BLE (ognuno col suo buffer, senza mescolare le righe) ----
struct LineIn {
  char buf[448];
  uint16_t len;
};
LineIn serIn, bleIn;

uint32_t lastDataAt = 0;             // ultimo dato arrivato dal PC
uint32_t bootStart = 0, bootEnd = 0; // animazione di avvio

int todayPrompts = 0, todayTools = 0, todayMin = 0;  // contatori di oggi

// eventi una tantum dal PC (commit, push, test, compattazione)
enum Fx { FX_NONE, FX_COMMIT, FX_PUSH, FX_TESTOK, FX_TESTFAIL, FX_COMPACT };
uint8_t fxKind = FX_NONE;
uint32_t fxStart = 0, fxUntil = 0;
long fxSeq = -1;

// Timeout
const uint32_t DONE_TO_IDLE_MS = 5000;
const uint32_t WORKING_STALE_MS = 180000;  // se manca lo Stop, torna a riposo
const uint32_t IDLE_TO_SLEEP_MS = 120000;

// ---- Colori ----
static inline uint16_t scaled(uint8_t r, uint8_t gg, uint8_t b, float k) {
  if (k < 0) k = 0;
  if (k > 1) k = 1;
  return RGB565((uint8_t)(r * k), (uint8_t)(gg * k), (uint8_t)(b * k));
}


// ---- Utilita' ----
void setBrightnessSmooth(uint8_t target) {
  if (curBrightness == target) return;
  int b = curBrightness;
  if (b < target) b = (b + 12 > target) ? target : b + 12;
  else b = (b - 6 < target) ? target : b - 6;
  curBrightness = (uint8_t)b;
  panel->setBrightness(curBrightness);
}

// ---- Standby / spegnimento ----
bool standby = false;
bool settingsOpen = false;  // menu impostazioni aperto
enum DemoKind : uint8_t { DM_NONE, DM_BORED, DM_TIRED, DM_FULL, DM_COLD, DM_LOWBAT };
bool demoActive = false;
int demoIdx = -1;
uint8_t demoKind = DM_NONE;
uint32_t demoRetrig = 0, demoEnds = 0;
#define demoOn(k) (demoActive && demoKind == (k))
uint16_t autoStandbyMin = 10;  // a batteria, dopo N minuti a riposo e senza tocchi; 0 = mai

void enterStandby() {
  if (standby) return;
  standby = true;
  settingsOpen = false;
  hwStandby(true);
  panel->setBrightness(0);
  curBrightness = 0;
  panel->displayOff();
  setCpuFrequencyMhz(80);
}

void exitStandby() {
  if (!standby) return;
  setCpuFrequencyMhz(240);
  panel->displayOn();
  standby = false;
  hwStandby(false);
  lastTouch = millis();
}

void powerOff() {
  g->fillScreen(RGB565_BLACK);
  centerText("Spegnimento...", 220, 3, RGB565(200, 205, 220));
  if (canvas) canvas->flush();
  delay(900);
  hwPowerOff();
  delay(1500);
  // ancora acceso: alimentato dall'USB, il PMU non toglie le tensioni. Si ripiega sullo standby.
  enterStandby();
}

void centerText(const char *s, int y, uint8_t size, uint16_t color) {
  g->setTextSize(size);
  g->setTextColor(color);
  int16_t x1, y1;
  uint16_t w, h;
  g->getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
  g->setCursor(CX - (int)w / 2, y);
  g->print(s);
}

void setState(State s, const char *d) {
  if (s != state) {
    if (s == S_ATTENTION) sfxPlay(SFX_ALERT);
    else if (s == S_DONE) sfxPlay(SFX_DONE);
    else if (s == S_ERROR) sfxPlay(SFX_ERROR);
  }
  state = s;
  stateSince = millis();
  lastEvent = stateSince;
  strncpy(detail, d ? d : "", sizeof(detail) - 1);
  detail[sizeof(detail) - 1] = 0;
}

// ---- Clawd ----
// Mascotte di Claude Code disegnata a blocchi: corpo rettangolare, due braccini,
// occhi a barretta e quattro zampette. U = lato del "pixel" in schermo.
const int U = 22;
const int BODY_TOP = CY - 90;                 // bordo alto del corpo a riposo
const int GROUND_Y = BODY_TOP + 8 * U + 8;    // dove cade l'ombra
uint16_t C_CLAWD = RGB565(215, 119, 87);  // cambia col colore scelto
const uint16_t C_EYE = RGB565(20, 14, 14);


int clockHour() {
  if (!clockValid) return -1;
  time_t tt = time(nullptr);
  struct tm tmv;
  gmtime_r(&tt, &tmv);
  return tmv.tm_hour;
}
bool isNight() {
  int h = clockHour();
  return h >= 23 || (h >= 0 && h < 6);
}
int effHat() {
  if (styleHat == 3) return petLevel >= 10 ? 2 : petLevel >= 3 ? 1 : 0;
  return HAT_LVL[styleHat] <= petLevel ? styleHat : 0;
}
bool effGlasses() {
  if (styleGlasses == 2) return petLevel >= 6;
  return styleGlasses == 1 && petLevel >= 6;
}

void drawClawd(const Pose &p) {
  int cx = CX + (int)p.dx;
  int bodyH = 6 * U - p.squash;
  int top = BODY_TOP + (int)p.dy + p.squash;
  int bottom = top + bodyH;
  int left = cx - 5 * U - p.puff;
  int right = cx + 5 * U + p.puff;

  // ombra: piu' piccola quando salta
  float lift = -p.dy;
  if (lift < 0) lift = 0;
  int sw = 5 * U - (int)(lift * 0.5f);
  g->fillEllipse(cx, GROUND_Y, sw, 9, RGB565(38, 38, 44));

  // zampette
  static const int legX[4] = { -4, -2, 1, 3 };
  for (int i = 0; i < 4; i++) {
    float s = sinf(p.legPhase + ((i & 1) ? PI : 0));
    int up = (p.legAmp > 0 && s > 0) ? (int)(p.legAmp * s) : 0;
    int len = 2 * U - up - p.squash;
    if (len < 6) len = 6;
    g->fillRect(cx + legX[i] * U, bottom - 1, U, len, C_CLAWD);
  }

  // corpo
  g->fillRect(left, top, 10 * U + 2 * p.puff, bodyH, C_CLAWD);

  // tastiera
  if (p.keyboard) {
    int ky = bottom - 44;
    g->fillRoundRect(cx - 150, ky, 300, 26, 5, RGB565(55, 58, 70));
    uint32_t seed = millis() / 90;
    for (int i = 0; i < 12; i++) {
      bool lit = ((seed * 7 + i * 13) % 5) == 0;
      g->fillRect(cx - 140 + i * 24, ky + 6, 18, 14, lit ? RGB565(255, 190, 120) : RGB565(95, 100, 118));
    }
  }

  // braccini
  const int aw = 34, ah = 40;
  int ayL = top + 2 * U - (int)(p.armL * 50);
  int ayR = top + 2 * U - (int)(p.armR * 50);
  g->fillRect(left - aw, ayL, aw, ah, C_CLAWD);
  g->fillRect(right, ayR, aw, ah, C_CLAWD);

  // occhi
  int ecy = top + (int)(bodyH * 0.42f) + (int)p.lookY;
  for (int side = -1; side <= 1; side += 2) {
    int ex = cx + side * 3 * U + (int)p.lookX;
    switch (p.eye) {
      case EYE_OPEN: {
        int h = (int)(2 * U * p.openK);
        if (h < 5) h = 5;
        g->fillRect(ex - 10, ecy - h / 2, 20, h, C_EYE);
        break;
      }
      case EYE_HAPPY:
        for (int k = -2; k <= 2; k++) {
          g->drawLine(ex - 15, ecy + 10 + k, ex, ecy - 8 + k, C_EYE);
          g->drawLine(ex, ecy - 8 + k, ex + 15, ecy + 10 + k, C_EYE);
        }
        break;
      case EYE_CLOSED:
        g->fillRect(ex - 14, ecy, 28, 5, C_EYE);
        break;
      case EYE_X:
        for (int k = -2; k <= 2; k++) {
          g->drawLine(ex - 12, ecy - 12 + k, ex + 12, ecy + 12 + k, C_EYE);
          g->drawLine(ex - 12, ecy + 12 + k, ex + 12, ecy - 12 + k, C_EYE);
        }
        break;
    }
  }

  if (p.brow) {  // sopracciglia arrabbiate (inclinate verso il centro)
    for (int side = -1; side <= 1; side += 2) {
      int ex = cx + side * 3 * U + (int)p.lookX;
      for (int k = -2; k <= 2; k++)
        g->drawLine(ex - side * 18, ecy - 40 + k, ex + side * 16, ecy - 26 + k, C_EYE);
    }
  }

  // accessori scelti (sbloccati col livello)
  if (effHat() == 2) {  // corona
    const uint16_t gold = RGB565(255, 205, 60);
    g->fillRect(cx - 3 * U, top - 14, 6 * U, 14, gold);
    for (int i = -1; i <= 1; i++)
      g->fillTriangle(cx + i * 2 * U - 14, top - 14, cx + i * 2 * U + 14, top - 14, cx + i * 2 * U, top - 36, gold);
  } else if (effHat() == 1) {  // cappellino con pompon
    g->fillRect(cx - 4 * U, top - 20, 8 * U, 20, RGB565(70, 120, 210));
    g->fillRect(cx - 14, top - 32, 28, 14, RGB565(235, 238, 245));
  }
  if (effGlasses() && p.eye != EYE_CLOSED) {  // occhiali
    const uint16_t fr = RGB565(235, 238, 245);
    for (int side = -1; side <= 1; side += 2) {
      int ex = cx + side * 3 * U + (int)p.lookX;
      for (int k = 0; k < 3; k++) g->drawRect(ex - 20 + k, ecy - 32 + k, 40 - 2 * k, 64 - 2 * k, fr);
    }
    g->fillRect(cx - 3 * U + 20 + (int)p.lookX, ecy - 3, 6 * U - 40, 4, fr);
  }
}

void drawThoughtBubble(float ts, int cx) {
  int bx = cx + 125, by = BODY_TOP - 40;
  const uint16_t W = RGB565(228, 234, 245);
  g->fillCircle(cx + 78, BODY_TOP + 6, 5, W);
  g->fillCircle(cx + 96, BODY_TOP - 14, 8, W);
  g->fillCircle(bx - 24, by + 6, 20, W);
  g->fillCircle(bx + 2, by - 10, 26, W);
  g->fillCircle(bx + 28, by + 6, 20, W);
  g->fillRect(bx - 24, by + 2, 52, 24, W);
  int active = (int)(ts * 3) % 3;
  for (int i = 0; i < 3; i++)
    g->fillCircle(bx - 18 + i * 20, by + 8, i == active ? 6 : 4, RGB565(90, 100, 130));
}

void drawSparks(float ts, int cx) {
  for (int i = 0; i < 6; i++) {
    float age = fmodf(ts * 1.6f + i * 0.17f, 1.0f);
    int x = cx + (i - 3) * 46 + (int)(10 * sinf(ts * 3 + i));
    int y = BODY_TOP - 6 - (int)(age * 70);
    int s = 3 + (int)(7 * (1.0f - age));
    g->fillRect(x, y, s, s, (i & 1) ? RGB565(255, 190, 120) : RGB565(255, 245, 230));
  }
}

void drawConfetti(float since) {
  static const uint16_t pal[5] = { RGB565(255, 120, 90), RGB565(255, 210, 70), RGB565(110, 230, 150),
                                   RGB565(110, 170, 255), RGB565(230, 130, 230) };
  for (int i = 0; i < 18; i++) {
    int x = CX + ((i * 37) % 300) - 150 + (int)(12 * sinf(since * 5 + i));
    int y = BODY_TOP - 70 + (int)(since * 110) + ((i * 29) % 60);
    if (y > 340) continue;
    g->fillRect(x, y, 8, 8, pal[i % 5]);
  }
}

// Clawd in miniatura: sub-agent (mode 0 = lavora) e timer (mode 1 = riposa)
void drawMini(int cx, int top, int u, float ts, float ph, int mode) {
  static const int legX[4] = { -4, -2, 1, 3 };
  top += (mode == 0) ? (int)(1.5f * sinf(ts * 18 + ph)) : 0;
  int left = cx - 5 * u;
  for (int i = 0; i < 4; i++) g->fillRect(cx + legX[i] * u, top + 6 * u - 1, u, 2 * u, C_CLAWD);
  g->fillRect(left, top, 10 * u, 6 * u, C_CLAWD);
  for (int side = -1; side <= 1; side += 2) {
    int ay = top + 2 * u + (mode == 0 ? (int)(u * sinf(ts * 18 + ph + (side > 0 ? PI : 0))) : 0);
    int ax = side < 0 ? left - 3 * u / 2 : cx + 5 * u;
    g->fillRect(ax, ay, 3 * u / 2, 2 * u, C_CLAWD);
    int h = (mode == 0) ? 3 * u / 2 : 2;
    g->fillRect(cx + side * 3 * u - u / 2, top + 2 * u, u, h < 3 ? 3 : h, C_EYE);
  }
}

void drawAgents(float ts) {
  int n = agentsN > 4 ? 4 : agentsN;
  for (int i = 0; i < n; i++)
    drawMini(CX - 70 + (int)((i - (n - 1) / 2.0f) * 64), 58, 4, ts, i * 1.7f, 0);
}

void drawSweat(int x, int y, float ts) {
  y += (int)(4 * sinf(ts * 5));
  const uint16_t c = RGB565(120, 190, 255);
  g->fillCircle(x, y + 10, 8, c);
  g->fillTriangle(x - 7, y + 7, x + 7, y + 7, x, y - 12, c);
}

void drawStar(int x, int y, uint16_t c) {
  g->fillRect(x - 6, y - 1, 12, 3, c);
  g->fillRect(x - 1, y - 6, 3, 12, c);
}

// ---- Batteria: indicatore, allarme e fine carica ----
uint32_t batAlertUntil = 0, batFullUntil = 0;  // animazioni "batteria scarica" / "carica completa"
const int BAT_LOW_PCT = 15;

bool batLowNow() {
  int bp = batPercent();
  return bp >= 0 && bp <= BAT_LOW_PCT && !usbPresent() && !batCharging();
}

// controlla i cambi di stato della batteria e fa partire le animazioni (una volta sola per evento)
void batEvents(uint32_t now) {
  static int prevPct = -1;
  static bool wasCharging = false, fullDone = false;
  int bp = batPercent();
  if (bp < 0) return;
  bool chg = batCharging(), usb = usbPresent();
  // scarica: attraversa il 15% e il 5%
  if (!usb && prevPct >= 0 && ((prevPct > BAT_LOW_PCT && bp <= BAT_LOW_PCT) || (prevPct > 5 && bp <= 5))) {
    batAlertUntil = now + 5000;
    lastTouch = now;
    sfxPlay(SFX_ERROR);
    if (state == S_SLEEP) setState(S_IDLE, "");
  }
  // carica: finita quando smette di caricare con l'USB collegato (o arriva al 100%)
  if (!usb || bp < 90) fullDone = false;
  if (usb && !fullDone && ((wasCharging && !chg && bp >= 90) || bp >= 100)) {
    fullDone = true;
    batFullUntil = now + 6000;
    lastTouch = now;
    sfxPlay(SFX_DONE);
    if (state == S_SLEEP) setState(S_IDLE, "");
  }
  wasCharging = chg;
  prevPct = bp;
}

// icona batteria di larghezza w (altezza ~ w/2): riempimento e colore secondo il livello
void drawBatteryIcon(int x, int y, int w, int pct, bool charging, bool lowBlink) {
  int h = w / 2 + 2;
  uint16_t col = pct <= BAT_LOW_PCT ? RGB565(255, 90, 80) : pct <= 35 ? RGB565(255, 190, 70) : RGB565(110, 220, 150);
  if (lowBlink && (millis() / 400) % 2) col = RGB565(90, 30, 30);
  uint16_t edge = RGB565(190, 195, 210);
  g->drawRoundRect(x, y, w, h, 3, edge);
  g->fillRect(x + w, y + h / 3, 3, h - 2 * (h / 3), edge);  // polo positivo
  int fw = (w - 6) * constrain(pct, 0, 100) / 100;
  if (fw > 0) g->fillRect(x + 3, y + 3, fw, h - 6, col);
  if (charging) {  // fulmine
    int bx = x + w / 2, by = y + 2;
    uint16_t bc = RGB565(255, 255, 255);
    g->fillTriangle(bx + 2, by, bx - 4, by + h / 2, bx + 1, by + h / 2, bc);
    g->fillTriangle(bx - 2, by + h - 4, bx + 4, by + h / 2 - 1, bx - 1, by + h / 2 - 1, bc);
  }
}

void centerTextAt(const char *s, int cx, int y, uint8_t size, uint16_t color) {
  g->setTextSize(size);
  g->setTextColor(color);
  int16_t x1, y1;
  uint16_t w, h;
  g->getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
  g->setCursor(cx - (int)w / 2, y);
  g->print(s);
}

// indicatore fisso nella schermata di Clawd (a destra dell'orologio)
void drawBatteryIndicator() {
  int bp = batPercent();
  if (bp < 0) return;
  bool low = bp <= BAT_LOW_PCT && !usbPresent();
  drawBatteryIcon(CX + 78, 62, 36, bp, batCharging() || usbPresent(), low);
  char b[8];
  snprintf(b, sizeof(b), "%d%%", bp);
  centerTextAt(b, CX + 96, 86, 2, low ? RGB565(255, 110, 100) : RGB565(150, 160, 185));
}

void drawTopRow(uint32_t t) {
  char buf[24];
  if (msgUnread) g->fillCircle(CX + 90, 28, 6, RGB565(255, 160, 80));  // messaggio non letto
  if (tmRunning) {
    int sec = tmLeftMs / 1000;
    snprintf(buf, sizeof(buf), "%s %02d:%02d", tmFocus ? "Focus" : "Pausa", sec / 60, sec % 60);
    centerText(buf, 22, 2, tmFocus ? RGB565(255, 150, 90) : RGB565(110, 230, 150));
    return;
  }
  int bp = batPercent();
  if (bp >= 0 && bp < 20 && !batCharging() && !usbPresent()) {
    snprintf(buf, sizeof(buf), "Batt %d%%", bp);
    centerText(buf, 22, 2, RGB565(255, 110, 100));
    return;
  }
  if (todoTotal > 0 && todoDone < todoTotal) {  // lista attivita' in corso
    snprintf(buf, sizeof(buf), "Task %d/%d", todoDone, todoTotal);
    centerText(buf, 20, 2, RGB565(130, 200, 255));
    const int W = 110, x = CX - W / 2;
    g->fillRect(x, 44, W, 4, RGB565(40, 44, 56));
    g->fillRect(x, 44, W * todoDone / todoTotal, 4, RGB565(130, 200, 255));
    return;
  }
  snprintf(buf, sizeof(buf), "Lv %d", petLevel);
  centerText(buf, 20, 2, RGB565(150, 160, 185));
  const int W = 90, x = CX - W / 2;
  g->fillRect(x, 44, W, 4, RGB565(40, 44, 56));
  g->fillRect(x, 44, (int)(W * petProg), 4, C_CLAWD);
}

// Riga scorrevole (finestra di 240 px: sta nel cerchio dello schermo). Un carattere = 12 px (size 2).
void drawTicker(uint32_t t, uint16_t col) {
  if (!ticker[0]) return;
  const int W = 240, X = CX - 120, Y = 414;
  int n = strlen(ticker);
  int textW = n * 12;
  if (textW <= W) {
    centerText(ticker, Y, 2, col);
    return;
  }
  int span = textW + 72;  // spazio tra una ripetizione e l'altra
  int off = (int)((t / 1000.0f) * 45.0f) % span;
  g->setTextSize(2);
  g->setTextColor(col);
  for (int pass = 0; pass < 2; pass++) {
    for (int i = 0; i < n; i++) {
      int x = X - off + pass * span + i * 12;
      if (x >= X && x + 12 <= X + W) {
        g->setCursor(x, Y);
        g->print(ticker[i]);
      }
    }
  }
}

int32_t cacheRemaining(const Sess &s) {
  if (s.cacheS < 0) return -1;
  int32_t left = s.cacheS - (int32_t)((millis() - s.cacheAt) / 1000);
  return left > 0 ? left : 0;
}

uint32_t projHash(const char *name) {
  uint32_t h = 5381;
  for (const char *p = name; *p; p++) h = h * 33 + (uint8_t)*p;
  return h;
}

// Clawd in miniatura col colore e il cappello del progetto (derivati dal nome della cartella)
void drawProjIcon(int cx, int top, const Sess &s, float ts) {
  static const uint16_t pal[8] = { RGB565(215, 119, 87), RGB565(90, 140, 220), RGB565(90, 185, 120), RGB565(160, 110, 210),
                                   RGB565(230, 120, 160), RGB565(230, 185, 70), RGB565(70, 190, 190), RGB565(200, 90, 90) };
  uint32_t h = projHash(s.name);
  uint16_t saved = C_CLAWD;
  C_CLAWD = pal[h % 8];
  drawMini(cx, top, 2, ts, (h % 7) * 0.9f, s.st == 'W' ? 0 : 1);
  int hat = (h >> 8) % 3;
  if (hat == 1) {
    g->fillRect(cx - 8, top - 4, 16, 4, RGB565(70, 120, 210));
  } else if (hat == 2) {
    const uint16_t gold = RGB565(255, 205, 60);
    g->fillRect(cx - 6, top - 4, 12, 4, gold);
    g->fillRect(cx - 6, top - 7, 3, 3, gold);
    g->fillRect(cx - 1, top - 7, 3, 3, gold);
    g->fillRect(cx + 4, top - 7, 3, 3, gold);
  }
  C_CLAWD = saved;
}

void drawCheckBadge(int x, int y) {
  g->fillRoundRect(x - 22, y - 22, 44, 44, 10, RGB565(60, 190, 110));
  for (int k = -2; k <= 2; k++) {
    g->drawLine(x - 11, y + k, x - 3, y + 9 + k, RGB565_WHITE);
    g->drawLine(x - 3, y + 9 + k, x + 12, y - 9 + k, RGB565_WHITE);
  }
}

// razzetto che parte dalla testa di Clawd (git push)
void drawRocket(int cx, float since) {
  float k = since / 3.2f;
  if (k > 1) return;
  int y = BODY_TOP - 60 - (int)(200.0f * k * k);
  if (y < -90) return;
  const uint16_t W = RGB565(235, 238, 245), R = RGB565(230, 80, 70);
  int fl = 14 + (int)(10 * fabsf(sinf(since * 40)));
  g->fillTriangle(cx - 9, y + 56, cx + 9, y + 56, cx, y + 56 + fl, RGB565(255, 170, 60));
  g->fillRect(cx - 12, y, 24, 56, W);
  g->fillTriangle(cx - 12, y + 2, cx + 12, y + 2, cx, y - 24, R);
  g->fillTriangle(cx - 12, y + 40, cx - 24, y + 62, cx - 12, y + 56, R);
  g->fillTriangle(cx + 12, y + 40, cx + 24, y + 62, cx + 12, y + 56, R);
  g->fillCircle(cx, y + 22, 7, RGB565(90, 160, 230));
}

void drawHeart(int x, int y, int s, uint16_t c) {
  g->fillCircle(x - s / 2, y, s / 2 + 1, c);
  g->fillCircle(x + s / 2, y, s / 2 + 1, c);
  g->fillTriangle(x - s - 1, y + 2, x + s + 1, y + 2, x, y + s + 6, c);
}

void drawClawdPage() {
  uint32_t t = millis();
  float ts = t / 1000.0f;
  float since = (ago(t, stateSince)) / 1000.0f;

  uint16_t bg = RGB565_BLACK;
  if ((int32_t)(alarmUntil - t) > 0) {  // allarme usage: sfondo rosso pulsante
    float k = 0.5f + 0.5f * sinf(ts * 9);
    bg = scaled(90, 10, 10, 0.2f + 0.8f * k);
  } else if (state == S_ATTENTION) {
    float k = 0.5f + 0.5f * sinf(ts * 8);
    bg = scaled(70, 50, 0, 0.2f + 0.8f * k);
  }
  g->fillScreen(bg);

  Pose p;
  bool blink = fmodf(ts, 4.2f) < 0.14f;
  bool night = isNight();
  bool bored = (state == S_IDLE && (ago(t, lastEvent)) > 60000 && !night) || demoOn(DM_BORED);
  bool full = (uctx >= 90 && (state == S_IDLE || state == S_THINKING || state == S_WORKING)) || demoOn(DM_FULL);  // contesto quasi pieno
  bool cold = (state == S_IDLE && sessN > 0 && cacheRemaining(sess[0]) == 0) || demoOn(DM_COLD);                  // cache prompt scaduta

  switch (state) {
    case S_IDLE: {
      // gira per la scrivania, ogni tanto saluta
      float w = sinf(ts / 2.5f);
      p.dx = 40 * w;
      p.lookX = (cosf(ts / 2.5f) > 0 ? 1 : -1) * 6;
      if (fabsf(cosf(ts / 2.5f)) > 0.35f) {
        p.legAmp = 8;
        p.legPhase = ts * 14;
      }
      p.dy = 2 * sinf(ts * 2.2f);
      p.armL = p.armR = 0.1f * sinf(ts * 2.2f);
      float ph = fmodf(ts, 10.0f);
      if (ph > 7.0f && ph < 8.6f) {
        p.armR = 0.8f + 0.2f * sinf(ts * 16);
        p.lookX = 0;
      }
      if (bored) {  // si annoia: si siede, si guarda intorno e ogni tanto sbadiglia
        p.dx = 0;
        p.legAmp = 0;
        p.squash = 12;
        p.lookX = 10 * sinf(ts * 0.8f);
        p.armL = p.armR = -0.2f;
        if (fmodf(ts, 9.0f) < 1.8f) {  // sbadiglio: si stiracchia
          p.eye = EYE_CLOSED;
          p.armL = p.armR = 0.95f;
          p.squash = 0;
          p.dy = -4;
        }
      }
      if (blink) p.openK = 0.1f;
      break;
    }
    case S_THINKING:
      p.dx = 6 * sinf(ts * 1.3f);
      p.lookX = 8 * sinf(ts * 1.3f);
      p.lookY = -10;
      p.dy = 2 * sinf(ts * 2.0f);
      p.armL = 0.55f + 0.05f * sinf(ts * 3);  // mano sul mento
      if (blink) p.openK = 0.1f;
      break;
    case S_WORKING:
      p.keyboard = true;
      p.lookY = 10;
      p.lookX = 10 * sinf(ts * 4);
      p.openK = 0.7f;
      p.armL = -0.35f + 0.3f * sinf(ts * 18);
      p.armR = -0.35f + 0.3f * sinf(ts * 18 + PI);
      p.dy = 1.5f * sinf(ts * 20);
      p.legAmp = 5;
      p.legPhase = ts * 20;
      break;
    case S_ATTENTION:
      p.dy = -34 * fabsf(sinf(ts * 7));
      p.armL = p.armR = 0.9f + 0.1f * sinf(ts * 22);
      p.openK = 1.2f;
      break;
    case S_DONE:
      p.eye = EYE_HAPPY;
      if (since < 1.8f) {
        p.dy = -30 * fabsf(sinf(since * 8)) * (1.0f - since / 1.8f);
        p.armL = p.armR = 0.9f + 0.1f * sinf(ts * 16);
      } else {
        p.armL = p.armR = 0.3f + 0.05f * sinf(ts * 3);
        p.dy = 2 * sinf(ts * 2.0f);
      }
      break;
    case S_SLEEP:
      p.eye = EYE_CLOSED;
      p.squash = 34;
      p.dy = 2 * sinf(ts * 1.2f);
      p.armL = p.armR = -0.2f;
      break;
    case S_ERROR:  // un tool e' fallito: perplesso, scuote la testa
      p.dx = 10 * sinf(ts * 16);
      p.lookX = 8 * sinf(ts * 9);
      p.openK = 1.15f;
      p.armL = 0.5f;
      p.armR = 0.2f;
      break;
  }

  // stanco se l'usage e' quasi al limite
  bool tired = demoOn(DM_TIRED) || (u5 >= 85 || u7 >= 92) &&
               (state == S_IDLE || state == S_THINKING || state == S_WORKING);
  if (tired) {
    p.openK *= 0.6f;
    p.legAmp *= 0.5f;
    p.dy += 3;
  }
  bool batLow = (batLowNow() && (state == S_IDLE || state == S_THINKING || state == S_WORKING)) || demoOn(DM_LOWBAT);
  if (batLow) {  // batteria scarica: si muove piano
    p.openK *= 0.55f;
    p.legAmp *= 0.3f;
    p.dy += 4;
    p.dx *= 0.4f;
  }
  if (full) p.puff = 10 + (int)(3 * sinf(ts * 3));
  if (cold) p.dx += 2 * sinf(ts * 45);  // trema dal freddo

  // scossa: stordito (ha la precedenza su tutto)
  bool dizzy = (int32_t)(dizzyUntil - t) > 0;
  if (dizzy) {
    p = Pose();
    p.eye = EYE_X;
    p.dx = 16 * sinf(ts * 9);
    p.armL = 0.3f * sinf(ts * 12);
    p.armR = 0.3f * sinf(ts * 12 + PI);
  }

  // livello salito: festeggia
  bool lvlUp = !dizzy && (int32_t)(lvlUpUntil - t) > 0;
  float lvlSince = 3.5f - (float)(int32_t)(lvlUpUntil - t) / 1000.0f;
  if (lvlUp) {
    p = Pose();
    p.eye = EYE_HAPPY;
    p.dy = -26 * fabsf(sinf(lvlSince * 8)) * (1.0f - lvlSince / 3.5f);
    p.armL = p.armR = 0.9f + 0.1f * sinf(ts * 16);
  }

  // usage: allarme (>= 90%) e limite azzerato
  bool uAlarm = !dizzy && !lvlUp && (int32_t)(alarmUntil - t) > 0;
  bool uReset = !dizzy && !lvlUp && !uAlarm && (int32_t)(resetUntil - t) > 0;
  float rsince = 4.5f - (float)(int32_t)(resetUntil - t) / 1000.0f;
  if (uAlarm) {
    p = Pose();
    p.dy = -20 * fabsf(sinf(ts * 10));
    p.armL = p.armR = 0.9f + 0.1f * sinf(ts * 22);
    p.openK = 1.25f;
  } else if (uReset) {
    p = Pose();
    p.eye = EYE_HAPPY;
    p.dy = -24 * fabsf(sinf(rsince * 8)) * (1.0f - rsince / 4.5f);
    p.armL = p.armR = 0.9f + 0.1f * sinf(ts * 16);
  }
  // batteria: fine carica (festa) e allarme scarica (si accascia, sudore)
  bool batFullOn = !dizzy && !lvlUp && !uAlarm && !uReset && (int32_t)(batFullUntil - t) > 0;
  bool batAlertOn = !dizzy && !lvlUp && !uAlarm && !uReset && !batFullOn && (int32_t)(batAlertUntil - t) > 0;
  float bsince = 6.0f - (float)(int32_t)(batFullUntil - t) / 1000.0f;
  if (batFullOn) {
    p = Pose();
    p.eye = EYE_HAPPY;
    p.dy = -26 * fabsf(sinf(bsince * 7)) * (1.0f - bsince / 6.0f * 0.6f);
    p.armL = p.armR = 0.9f + 0.1f * sinf(ts * 16);
  } else if (batAlertOn) {
    p = Pose();
    p.openK = 0.55f;
    p.squash = 14;
    p.dx = 4 * sinf(ts * 22);
    p.dy = 4;
    p.armL = p.armR = -0.35f;
  }
  // eventi una tantum: commit, push, test ok, compattazione (il test fallito usa la faccia di errore)
  bool fxOn = !dizzy && !lvlUp && !uAlarm && !uReset && !batFullOn && !batAlertOn && fxKind != FX_NONE && (int32_t)(fxUntil - t) > 0;
  float fsince = (t - fxStart) / 1000.0f;
  if (fxOn) {
    p = Pose();
    switch (fxKind) {
      case FX_COMMIT:
        p.eye = EYE_HAPPY;
        p.dy = -22 * fabsf(sinf(fsince * 9)) * (1.0f - fsince / 3.0f);
        p.armL = p.armR = 0.9f + 0.1f * sinf(ts * 16);
        break;
      case FX_PUSH:
        p.lookY = -14;
        p.dy = 2 * sinf(ts * 6);
        p.armL = p.armR = 0.9f + 0.1f * sinf(ts * 18);
        break;
      case FX_TESTOK:
        p.eye = EYE_HAPPY;
        p.armR = 1.0f;  // pollice in su
        p.dy = 2 * sinf(ts * 3);
        break;
      case FX_COMPACT:
        p.squash = 40 + (int)(28 * sinf(ts * 8));
        p.puff = p.squash / 4;
        p.openK = 0.4f;
        p.armL = p.armR = -0.2f;
        break;
    }
  }
  bool busy = dizzy || lvlUp || uAlarm || uReset || fxOn || batFullOn || batAlertOn;

  // tocchi ripetuti: prima ride, poi si infastidisce
  bool annoyed = !busy && (int32_t)(annoyUntil - t) > 0;
  bool laugh = !busy && !annoyed && (int32_t)(laughUntil - t) > 0;
  if (annoyed) {
    p = Pose();
    p.brow = 1;
    p.openK = 0.5f;
    p.dx = 5 * sinf(ts * 30);
    p.armL = p.armR = -0.3f;
  } else if (laugh) {
    p = Pose();
    p.eye = EYE_HAPPY;
    p.dy = -12 * fabsf(sinf(ts * 18));
    p.armL = 0.6f + 0.4f * sinf(ts * 20);
    p.armR = 0.6f + 0.4f * sinf(ts * 20 + PI);
  }
  // mezzanotte: animazione speciale
  bool midnight = !busy && !annoyed && !laugh && (int32_t)(midnightUntil - t) > 0 &&
                  (state == S_IDLE || state == S_DONE);
  if (midnight) {
    p = Pose();
    p.eye = EYE_HAPPY;
    p.dy = 2 * sinf(ts * 3);
    p.armL = p.armR = 0.9f + 0.1f * sinf(ts * 5);
  }

  // carezza: solo se Clawd non sta lavorando
  bool petting = !busy && !annoyed && !laugh && !midnight && (int32_t)(petUntil - t) > 0 &&
                 (state == S_IDLE || state == S_DONE);
  if (petting) {
    float k = 1.0f - (float)(int32_t)(petUntil - t) / 1600.0f;  // 0..1
    p = Pose();
    p.dx = 0;
    p.eye = EYE_HAPPY;
    p.dy = -16 * fabsf(sinf(k * 14));
    p.armL = p.armR = 0.7f + 0.3f * sinf(ts * 14);
  }

  drawClawd(p);

  // decorazioni
  int cx = CX + (int)p.dx;
  if (petting) {
    float k = 1.0f - (float)(int32_t)(petUntil - t) / 1600.0f;
    for (int i = 0; i < 3; i++) {
      float age = fmodf(k * 1.6f + i * 0.33f, 1.0f);
      drawHeart(cx + (i - 1) * 54 + (int)(8 * sinf(ts * 6 + i)), BODY_TOP - 8 - (int)(age * 60), 9,
                scaled(255, 110, 140, 1.0f - age * 0.7f));
    }
  }
  if (dizzy) {
    for (int i = 0; i < 3; i++) {
      float a = ts * 6 + i * 2.09f;
      drawStar(cx + (int)(78 * cosf(a)), BODY_TOP - 26 + (int)(16 * sinf(a)), RGB565(255, 220, 80));
    }
  }
  if (lvlUp) drawConfetti(lvlSince);
  if (batFullOn) {
    drawConfetti(bsince);
    drawBatteryIcon(cx - 28, BODY_TOP - 74, 56, 100, true, false);
    for (int i = 0; i < 4; i++) {
      float k = 0.4f + 0.6f * fabsf(sinf(ts * 4 + i * 1.7f));
      drawStar(cx - 90 + i * 60, BODY_TOP - 40 - (i % 2) * 24, scaled(255, 240, 160, k));
    }
  }
  if (batAlertOn) {
    drawBatteryIcon(cx - 28, BODY_TOP - 74, 56, batPercent() < 0 ? 0 : batPercent(), false, true);
    drawSweat(cx + 86, BODY_TOP + 8 + (int)p.dy, ts);
  }
  if (batLow && !batAlertOn) drawSweat(cx + 86, BODY_TOP + 8 + (int)p.dy, ts);
  if (uReset) drawConfetti(rsince);
  if (uAlarm) {
    drawSweat(cx + 86, BODY_TOP + 8 + (int)p.dy, ts);
    drawStar(cx - 90, BODY_TOP + 10, RGB565(255, 90, 80));
    drawStar(cx - 102, BODY_TOP - 6, RGB565(255, 90, 80));
  }
  if (annoyed) {  // "vena" da cartone animato
    drawStar(cx + 90, BODY_TOP + 6, RGB565(255, 80, 70));
    drawStar(cx + 100, BODY_TOP - 8, RGB565(255, 80, 70));
  }
  if (laugh) {
    for (int i = 0; i < 3; i++) {
      float age = fmodf(ts * 2.0f + i * 0.33f, 1.0f);
      g->setTextSize(2);
      g->setTextColor(scaled(255, 230, 120, 1.0f - age));
      g->setCursor(cx + (i - 1) * 70 - 12, BODY_TOP - 10 - (int)(age * 50));
      g->print("ha");
    }
  }
  if (midnight) {  // luna e stelle che brillano
    g->fillCircle(cx + 96, BODY_TOP - 30, 26, RGB565(235, 232, 200));
    g->fillCircle(cx + 110, BODY_TOP - 38, 24, RGB565_BLACK);
    for (int i = 0; i < 6; i++) {
      float k = 0.4f + 0.6f * fabsf(sinf(ts * 2 + i));
      drawStar(CX - 150 + i * 56, BODY_TOP - 62 - (i % 3) * 14, scaled(255, 240, 160, k));
    }
  }
  if (tired) drawSweat(cx + 86, BODY_TOP + 8 + (int)p.dy, ts);
  if (full) drawSweat(cx - 96, BODY_TOP + 24, ts);
  if (cold) {  // fiocchi di neve
    drawStar(cx - 104, BODY_TOP + 20 + (int)(6 * sinf(ts * 2)), RGB565(150, 200, 255));
    drawStar(cx + 108, BODY_TOP + 64 + (int)(6 * sinf(ts * 2 + 1)), RGB565(150, 200, 255));
  }
  if (fxOn) {
    switch (fxKind) {
      case FX_COMMIT:
        drawConfetti(fsince);
        drawCheckBadge(cx + 84, BODY_TOP - 44);
        break;
      case FX_PUSH:
        drawRocket(cx, fsince);
        break;
      case FX_TESTOK: {
        drawCheckBadge(cx + 84, BODY_TOP - 44);
        int ay = BODY_TOP + (int)p.dy + 2 * U - 50;  // il pollice sta sopra il braccio destro alzato
        g->fillRect(cx + 5 * U + 10, ay - 20, 14, 22, C_CLAWD);
        break;
      }
      case FX_COMPACT:
        for (int i = 0; i < 8; i++) {  // frammenti che si stringono verso il centro
          float a = i * 0.785f, r = 150.0f * (1.0f - fmodf(ts * 1.2f + i * 0.125f, 1.0f));
          g->fillRect(cx + (int)(r * cosf(a)) - 3, BODY_TOP + 66 + (int)(0.8f * r * sinf(a)) - 3, 6, 6, RGB565(255, 200, 150));
        }
        break;
    }
  }
  if (agentsN > 0 && (state == S_WORKING || state == S_THINKING)) drawAgents(ts);
  switch (state) {
    case S_ERROR: {
      drawSweat(cx + 86, BODY_TOP + 8, ts);
      g->setTextSize(6);
      g->setTextColor(RGB565(255, 210, 60));
      g->setCursor(cx - 18, BODY_TOP - 66);
      g->print("?");
      break;
    }
    case S_THINKING:
      drawThoughtBubble(ts, cx);
      break;
    case S_WORKING:
      drawSparks(ts, cx);
      break;
    case S_ATTENTION: {
      int y = BODY_TOP - 70 + (int)p.dy;
      g->fillRect(cx - 8, y, 16, 34, RGB565(255, 210, 60));
      g->fillRect(cx - 8, y + 42, 16, 14, RGB565(255, 210, 60));
      break;
    }
    case S_DONE:
      if (since < 2.5f) drawConfetti(since);
      break;
    case S_SLEEP:
      for (int i = 0; i < 3; i++) {
        float age = fmodf(ts / 3.0f + i / 3.0f, 1.0f);
        g->setTextSize(2 + (i % 2));
        g->setTextColor(scaled(110, 130, 180, 1.0f - age));
        g->setCursor(cx + 70 + (int)(age * 30), BODY_TOP + 30 - (int)(age * 60));
        g->print("z");
      }
      break;
    default:
      break;
  }

  // --- testi ---
  bool overlay = dizzy || lvlUp || uAlarm || uReset || fxOn || annoyed || laugh || midnight || batFullOn || batAlertOn;
  if (!overlay) switch (state) {
    case S_IDLE:
      if (clockText[0]) centerText(clockText, 62, 4, RGB565(150, 170, 200));
      if (batLow) {
        centerText("Batteria scarica", 372, 2, RGB565(255, 110, 100));
      } else if (full) {
        centerText("Memoria piena", 372, 2, RGB565(255, 130, 110));
      } else if (tired) {
        char b2[28];
        snprintf(b2, sizeof(b2), "Stanco... usage %d%%", demoOn(DM_TIRED) ? 88 : (u5 >= 85 ? u5 : u7));
        centerText(b2, 372, 2, RGB565(150, 170, 210));
      } else if (cold) {
        centerText("Cache fredda", 372, 2, RGB565(140, 190, 255));
      } else if (night && !demoOn(DM_BORED)) {
        centerText("Buonanotte", 372, 3, RGB565(120, 130, 170));
      } else if (bored) {
        centerText("Mi annoio...", 372, 2, RGB565(130, 140, 165));
      } else {
        centerText("In attesa", 372, 3, RGB565(120, 130, 150));
      }
      break;
    case S_ERROR:
      centerText("Ops!", 372, 3, RGB565(255, 130, 110));
      break;
    case S_THINKING:
      {
        char buf[12] = "Penso";  // "Penso" + 3 punti + \0 = 9 byte
        int dots = (t / 350) % 4;
        for (int i = 0; i < dots; i++) strcat(buf, ".");
        centerText(buf, 372, 3, RGB565(140, 180, 255));
      }
      break;
    case S_WORKING:
      centerText("Lavoro", 360, 3, RGB565(255, 170, 90));
      if (detail[0]) centerText(detail, 392, 2, RGB565(200, 140, 90));
      break;
    case S_ATTENTION:
      centerText("Serve te!", 366, 3, RGB565(255, 210, 60));
      if (detail[0]) centerText(detail, 400, 2, RGB565(220, 190, 90));
      break;
    case S_DONE:
      centerText("Fatto!", 372, 3, RGB565(110, 230, 150));
      break;
    case S_SLEEP:
      if (clockText[0]) centerText(clockText, 100, 3, RGB565(70, 80, 100));
      centerText("z z z", 372, 3, RGB565(60, 70, 90));
      break;
  }
  if (dizzy) centerText("Che giramenti!", 372, 2, RGB565(255, 220, 100));
  else if (batFullOn) centerText("Carica completa!", 372, 3, RGB565(110, 230, 150));
  else if (batAlertOn) {
    centerText("Batteria scarica!", 372, 3, RGB565(255, 110, 100));
    centerText("Collegami al cavo", 404, 2, RGB565(200, 140, 130));
  } else if (uAlarm) {
    char b4[24];
    snprintf(b4, sizeof(b4), "Usage al %d%%!", alarmPct);
    centerText(b4, 372, 3, RGB565(255, 110, 100));
  } else if (uReset) centerText("Puoi ripartire!", 372, 3, RGB565(110, 230, 150));
  else if (fxOn) {
    const char *ft = fxKind == FX_COMMIT ? "Commit!" : fxKind == FX_PUSH ? "Push!" : fxKind == FX_TESTOK ? "Test ok!" : "Compatto...";
    centerText(ft, 372, 3, RGB565(235, 232, 200));
  }
  else if (annoyed) centerText("Basta!", 372, 3, RGB565(255, 120, 100));
  else if (laugh) centerText("Ahah!", 372, 3, RGB565(255, 230, 120));
  else if (midnight) centerText("Mezzanotte!", 372, 3, RGB565(235, 232, 200));
  if (lvlUp) {
    char b3[20];
    snprintf(b3, sizeof(b3), "Livello %d!", petLevel);
    centerText(b3, 372, 3, RGB565(255, 220, 100));
  }
  if (!overlay) {
    switch (state) {
      case S_WORKING: drawTicker(t, RGB565(190, 140, 100)); break;
      case S_IDLE:
      case S_DONE:
      case S_THINKING:
      case S_ERROR: drawTicker(t, RGB565(105, 115, 140)); break;
      default: break;
    }
  }
  if ((int32_t)(toastUntil - t) > 0) centerText(toast, 330, 2, RGB565(255, 230, 140));
  drawTopRow(t);
  if (!batFullOn && !batAlertOn) drawBatteryIndicator();
}

// ---- Pagina Sessioni ----
static const char *stateWord(char s) {
  switch (s) {
    case 'W': return "Lavoro";
    case 'T': return "Penso";
    case 'A': return "Serve te!";
    case 'D': return "Fatto";
    default: return "A riposo";
  }
}

static uint16_t stateColor(char s, float k = 1.0f) {
  switch (s) {
    case 'W': return scaled(255, 160, 80, k);
    case 'T': return scaled(130, 170, 255, k);
    case 'A': return scaled(255, 210, 60, k);
    case 'D': return scaled(110, 230, 150, k);
    default: return scaled(120, 130, 150, k);
  }
}

void rightText(const char *s, int xRight, int y, uint8_t size, uint16_t color) {
  g->setTextSize(size);
  g->setTextColor(color);
  int16_t x1, y1;
  uint16_t w, h;
  g->getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
  g->setCursor(xRight - (int)w, y);
  g->print(s);
}

void drawSessionsPage() {
  uint32_t t = millis();
  g->fillScreen(RGB565_BLACK);
  char buf[24];
  snprintf(buf, sizeof(buf), "Sessioni: %d", sessN);
  centerText(buf, 52, 3, RGB565(200, 205, 220));
  if (sessN == 0) {
    centerText("Nessuna sessione", 200, 2, RGB565(120, 130, 150));
    centerText("di Claude Code", 226, 2, RGB565(120, 130, 150));
    return;
  }
  for (int i = 0; i < sessN; i++) {
    const Sess &s = sess[i];
    int y = 100 + i * 60;
    uint16_t col = stateColor(s.st);
    float pulse = (s.st == 'A') ? 0.6f + 0.4f * sinf(t / 130.0f) : (s.st == 'W' || s.st == 'T') ? 0.7f + 0.3f * sinf(t / 250.0f) : 1.0f;
    drawProjIcon(56, y + 8, s, t / 1000.0f);
    g->fillCircle(86, y + 16, 8, stateColor(s.st, pulse));
    g->setTextSize(2);
    g->setTextColor(RGB565(225, 228, 238));
    g->setCursor(100, y);
    g->print(s.name);
    rightText(stateWord(s.st), 400, y, 2, col);
    // seconda riga: tool in uso (o messaggio) a sinistra, contesto a destra
    if (s.tool[0]) {
      g->setTextSize(2);
      g->setTextColor(RGB565(170, 130, 95));
      g->setCursor(100, y + 24);
      g->print(s.tool);
    }
    if (s.ctx >= 0) {
      snprintf(buf, sizeof(buf), "ctx %d%%", s.ctx);
      rightText(buf, 400, y + 24, 2, s.ctx >= 85 ? RGB565(255, 120, 100) : RGB565(105, 115, 135));
    }
    // terza riga: modello/effort a sinistra, cache prompt a destra
    char info[28] = "";
    if (s.model[0]) snprintf(info, sizeof(info), "%s%s%s%s", s.model, s.effort[0] ? " " : "", s.effort, s.fast ? " fast" : "");
    g->setTextSize(1);
    g->setTextColor(RGB565(95, 105, 125));
    g->setCursor(100, y + 44);
    g->print(info);
    int32_t cr = cacheRemaining(s);
    if (cr >= 0) {
      char cb[20];
      if (cr == 0) snprintf(cb, sizeof(cb), "cache fredda");
      else snprintf(cb, sizeof(cb), "cache %d:%02d", (int)(cr / 60), (int)(cr % 60));
      rightText(cb, 400, y + 44, 1, cr == 0 ? RGB565(120, 170, 255) : cr < 60 ? RGB565(255, 190, 90) : RGB565(110, 200, 140));
    }
  }
}

// ---- Pagina Usage ----
void formatRemaining(int mins, char *out, size_t n) {
  if (mins < 0) snprintf(out, n, "--");
  else if (mins >= 1440) snprintf(out, n, "%dg %dh", mins / 1440, (mins % 1440) / 60);
  else if (mins >= 60) snprintf(out, n, "%dh %02dm", mins / 60, mins % 60);
  else snprintf(out, n, "%dm", mins);
}

void drawBar(int y, const char *label, int pctv, int minsLeft) {
  const int X = 83, W = 300, H = 22;
  g->setTextSize(2);
  g->setTextColor(RGB565(200, 205, 220));
  g->setCursor(X, y - 28);
  g->print(label);
  char buf[16];
  if (pctv >= 0) snprintf(buf, sizeof(buf), "%d%%", pctv);
  else snprintf(buf, sizeof(buf), "n/d");
  rightText(buf, X + W, y - 28, 2, RGB565(225, 228, 238));
  g->fillRoundRect(X, y, W, H, 8, RGB565(34, 36, 46));
  if (pctv >= 0) {
    int p = pctv > 100 ? 100 : pctv;
    int w = W * p / 100;
    uint16_t c = pctv >= 85 ? RGB565(255, 100, 90) : pctv >= 60 ? RGB565(255, 190, 70) : RGB565(110, 220, 150);
    if (w > 4) g->fillRoundRect(X, y, w, H, 8, c);
  }
  if (minsLeft >= 0) {
    char r[16];
    formatRemaining(minsLeft, r, sizeof(r));
    snprintf(buf, sizeof(buf), "reset %s", r);
    g->setTextSize(1);
    g->setTextColor(RGB565(110, 120, 140));
    g->setCursor(X, y + H + 5);
    g->print(buf);
  }
}

void drawUsagePage() {
  g->fillScreen(RGB565_BLACK);
  centerText("Usage", 48, 3, RGB565(200, 205, 220));
  uint32_t el = usageAt ? (millis() - usageAt) / 60000 : 0;  // minuti dall'ultimo dato
  int m5 = (u5min >= 0) ? max(0, u5min - (int)el) : -1;
  int m7 = (u7min >= 0) ? max(0, u7min - (int)el) : -1;
  drawBar(135, "Sessione 5 ore", u5, m5);
  drawBar(235, "Settimana", u7, m7);
  drawBar(335, "Contesto", uctx, -1);
  char buf[40];
  snprintf(buf, sizeof(buf), "Oggi $%d.%02d  +%d -%d", statCents / 100, statCents % 100, statAdd, statRm);
  centerText(buf, 376, 2, RGB565(170, 175, 195));
  int bp = batPercent();
  if (bp >= 0) {
    snprintf(buf, sizeof(buf), "Batt %d%%%s", bp, batCharging() ? " carica" : usbPresent() ? " USB" : "");
    centerText(buf, 402, 2, bp < 20 && !usbPresent() ? RGB565(255, 110, 100) : RGB565(130, 140, 165));
  }
  if (u5 < 0 && u7 < 0) centerText("limiti: solo abbonati", 424, 1, RGB565(90, 100, 120));
}

// ---- Pagina Oggi: riepilogo della giornata ----
void drawTodayPage() {
  g->fillScreen(RGB565_BLACK);
  centerText("Oggi", 40, 3, RGB565(200, 205, 220));
  const char *labels[6] = { "Prompt", "Tool", "Lavoro", "Costo", "Righe", "Livello" };
  char v[6][24];
  snprintf(v[0], sizeof(v[0]), "%d", todayPrompts);
  snprintf(v[1], sizeof(v[1]), "%d", todayTools);
  if (todayMin >= 60) snprintf(v[2], sizeof(v[2]), "%dh %02dm", todayMin / 60, todayMin % 60);
  else snprintf(v[2], sizeof(v[2]), "%dm", todayMin);
  snprintf(v[3], sizeof(v[3]), "$%d.%02d", statCents / 100, statCents % 100);
  snprintf(v[4], sizeof(v[4]), "+%d -%d", statAdd, statRm);
  snprintf(v[5], sizeof(v[5]), "%d", petLevel);
  for (int i = 0; i < 6; i++) {
    int y = 92 + i * 46;
    g->setTextSize(2);
    g->setTextColor(RGB565(130, 140, 165));
    g->setCursor(83, y + 8);
    g->print(labels[i]);
    rightText(v[i], 383, y, 3, RGB565(230, 232, 240));
  }
  const int W = 200, x = CX - W / 2;
  g->fillRect(x, 376, W, 5, RGB565(40, 44, 56));
  g->fillRect(x, 376, (int)(W * petProg), 5, C_CLAWD);
}

// ---- Pagina Timer (pomodoro 25/5) ----
void drawTimerPage() {
  uint32_t t = millis();
  float ts = t / 1000.0f;
  g->fillScreen(RGB565_BLACK);
  int32_t total = tmFocus ? TM_FOCUS_MS : TM_BREAK_MS;
  float frac = 1.0f - (float)tmLeftMs / (float)total;
  if (frac < 0) frac = 0;
  int lit = (int)(frac * 60);
  uint16_t on = tmFocus ? RGB565(255, 150, 90) : RGB565(110, 230, 150);
  for (int i = 0; i < 60; i++) {
    float a = (2.0f * PI * i) / 60 - PI / 2;
    int x = CX + (int)(205 * cosf(a)), y = CY + (int)(205 * sinf(a));
    g->fillCircle(x, y, i < lit ? 4 : 2, i < lit ? on : RGB565(45, 48, 60));
  }
  centerText(tmFocus ? "Focus" : "Pausa", 92, 3, on);
  char buf[12];
  int sec = (tmLeftMs + 999) / 1000;
  snprintf(buf, sizeof(buf), "%02d:%02d", sec / 60, sec % 60);
  centerText(buf, 140, 7, RGB565(230, 232, 240));
  // Clawd in miniatura: lavora durante il focus, riposa durante la pausa
  drawMini(CX, 250, 6, ts, 0, (tmFocus && tmRunning) ? 0 : 1);
  centerText(tmRunning ? "tocca: pausa" : "tocca: avvia", 340, 2, RGB565(150, 160, 185));
  centerText("tieni premuto: reset", 366, 1, RGB565(90, 100, 120));
}

// ---- Pagina Messaggio: ultimo messaggio di Claude, con a-capo e scorrimento ----
void drawMsgPage() {
  g->fillScreen(RGB565_BLACK);
  centerText("Ultimo messaggio", 40, 2, RGB565(200, 205, 220));
  if (!msgText[0]) {
    centerText("Nessun messaggio", 210, 2, RGB565(120, 130, 150));
    return;
  }
  char buf[40];
  uint32_t age = msgAgeS + (millis() - msgRecvAt) / 1000;
  if (age < 60) snprintf(buf, sizeof(buf), "%s - ora", msgName);
  else if (age < 3600) snprintf(buf, sizeof(buf), "%s - %u min fa", msgName, (unsigned)(age / 60));
  else snprintf(buf, sizeof(buf), "%s - %u h fa", msgName, (unsigned)(age / 3600));
  centerText(buf, 68, 2, RGB565(130, 140, 165));

  // a-capo per parole: 25 caratteri per riga (size 2 = 12 px/carattere)
  const int COLS = 25, MAXL = 40;
  int start[MAXL], len[MAXL], n = 0;
  int i = 0, L = strlen(msgText);
  while (i < L && n < MAXL) {
    while (i < L && msgText[i] == ' ') i++;
    int end = i + COLS;
    if (end >= L) end = L;
    else {
      int sp = end;
      while (sp > i && msgText[sp] != ' ') sp--;
      if (sp > i) end = sp;
    }
    start[n] = i;
    len[n] = end - i;
    n++;
    i = end;
  }
  const int VIS = 11, STEP = 24, Y0 = 104;
  if (msgScroll > n - VIS) msgScroll = n - VIS;
  if (msgScroll < 0) msgScroll = 0;
  g->setTextSize(2);
  g->setTextColor(RGB565(225, 228, 238));
  for (int r = 0; r < VIS && msgScroll + r < n; r++) {
    int li = msgScroll + r;
    char line[COLS + 1];
    memcpy(line, msgText + start[li], len[li]);
    line[len[li]] = 0;
    g->setCursor(CX - 150, Y0 + r * STEP);
    g->print(line);
  }
  if (msgScroll > 0) g->fillTriangle(CX - 8, 98, CX + 8, 98, CX, 88, RGB565(130, 140, 165));
  if (msgScroll + VIS < n) g->fillTriangle(CX - 8, 392, CX + 8, 392, CX, 402, RGB565(130, 140, 165));
}

// ---- Pagina Stile: colore, cappello, occhiali, suono (tocca una riga per cambiare) ----
void saveStyle() {
  Preferences pf;
  pf.begin("claudebuddy", false);
  pf.putUChar("col", styleColor);
  pf.putUChar("hat", styleHat);
  pf.putUChar("gls", styleGlasses);
  pf.end();
}

// bottoni della pagina Stile (2x2, sotto Clawd)
static const int STY_X[2] = { -148, 6 };
static const int STY_Y[2] = { 290, 352 };
const int STY_W = 142, STY_H = 54;
uint32_t styFlashUntil = 0;
int styFlashId = -1;

void drawStylePage() {
  float ts = millis() / 1000.0f;
  uint32_t t = millis();
  g->fillScreen(RGB565_BLACK);
  Pose p;
  p.dy = -62 + 2 * sinf(ts * 2.2f);
  p.armL = p.armR = 0.1f * sinf(ts * 2.2f);
  if (fmodf(ts, 4.2f) < 0.14f) p.openK = 0.1f;
  drawClawd(p);
  const char *labels[4] = { "COLORE", "CAPPELLO", "OCCHIALI", "SUONO" };
  const char *vals[4] = { COLORS[styleColor].name, HAT_NAMES[styleHat], GLS_NAMES[styleGlasses], audioEnabled() ? "acceso" : "spento" };
  for (int i = 0; i < 4; i++) {
    int x = CX + STY_X[i & 1], y = STY_Y[i >> 1];
    bool fl = (int32_t)(styFlashUntil - t) > 0 && styFlashId == i;
    g->fillRoundRect(x, y, STY_W, STY_H, 14, fl ? RGB565(90, 100, 135) : RGB565(36, 42, 62));
    g->setTextSize(1);
    g->setTextColor(RGB565(130, 140, 165));
    g->setCursor(x + 14, y + 9);
    g->print(labels[i]);
    g->setTextSize(2);
    g->setTextColor(RGB565(235, 238, 248));
    g->setCursor(x + 14, y + 26);
    g->print(vals[i]);
    if (i == 0) g->fillCircle(x + STY_W - 22, y + STY_H / 2, 10, COLORS[styleColor].c);  // anteprima del colore
  }
  char lv[16];
  snprintf(lv, sizeof(lv), "Livello %d", petLevel);
  centerText(lv, 418, 1, RGB565(110, 120, 140));
}

// ---- Animazione di avvio: Clawd "nasce" ----
void drawBoot() {
  uint32_t t = millis();
  float k = (t - bootStart) / 1000.0f;
  float ts = t / 1000.0f;
  g->fillScreen(RGB565_BLACK);
  Pose p;
  float grow = k / 1.2f;
  if (grow > 1) grow = 1;
  float e = 1.0f - (1.0f - grow) * (1.0f - grow);
  p.squash = (int)((1.0f - e) * 100);  // il corpo sale da basso fino all'altezza piena
  p.eye = k < 1.5f ? EYE_CLOSED : EYE_OPEN;
  if (k >= 1.5f && k < 1.65f) p.openK = 0.15f;  // primo battito di ciglia
  if (k > 1.2f && k < 2.2f) p.dy = -18 * fabsf(sinf((k - 1.2f) * 6.2832f)) * (2.2f - k);
  if (k > 2.2f) p.armR = 0.8f + 0.2f * sinf(ts * 16);  // saluta
  drawClawd(p);
  if (k < 1.4f) {  // scintille mentre cresce
    for (int i = 0; i < 6; i++) {
      float a = k * 4 + i * 1.05f;
      drawStar(CX + (int)(130 * cosf(a)), BODY_TOP + 80 + (int)(70 * sinf(a)), scaled(255, 220, 150, 1.0f - k / 1.4f));
    }
  }
  if (k > 1.6f) {
    centerText("Claude Buddy", 366, 3, RGB565(230, 232, 240));
    centerText("v" FW_VERSION, 400, 2, RGB565(120, 130, 150));
  }
}

// ---- Pagina Info: stato di PC, chip, memoria ----
static void statusDot(int x, int y, const char *label, bool ok) {
  g->fillCircle(x, y + 7, 6, ok ? RGB565(110, 220, 150) : RGB565(255, 100, 90));
  g->setTextSize(2);
  g->setTextColor(RGB565(200, 205, 220));
  g->setCursor(x + 16, y);
  g->print(label);
}

void drawInfoPage() {
  uint32_t t = millis();
  g->fillScreen(RGB565_BLACK);
  centerText("Info", 34, 3, RGB565(200, 205, 220));
  centerText("Claude Buddy v" FW_VERSION, 68, 2, RGB565(130, 140, 165));
  char buf[40];
  if (lastDataAt) {
    uint32_t sAgo = (t - lastDataAt) / 1000;
    if (sAgo < 90) snprintf(buf, sizeof(buf), "PC: dati %us fa", (unsigned)sAgo);
    else snprintf(buf, sizeof(buf), "PC: dati %um fa", (unsigned)(sAgo / 60));
    centerText(buf, 100, 2, sAgo < 600 ? RGB565(110, 220, 150) : RGB565(255, 190, 90));
  } else {
    centerText("PC: nessun dato", 100, 2, RGB565(255, 110, 100));
  }
  uint32_t up = t / 1000;
  snprintf(buf, sizeof(buf), "Acceso: %uh %02um", (unsigned)(up / 3600), (unsigned)((up % 3600) / 60));
  centerText(buf, 126, 2, RGB565(170, 175, 195));
  if (clockValid) snprintf(buf, sizeof(buf), "Ora: %s (%s)", clockText, rtcPresent() ? "RTC" : "PC");
  else snprintf(buf, sizeof(buf), "Ora: non impostata");
  centerText(buf, 152, 2, RGB565(170, 175, 195));
  statusDot(CX - 110, 190, "Touch", touchOk);
  statusDot(CX + 22, 190, "Audio", hwAudioOk());
  statusDot(CX - 110, 218, "Moto", hwImuOk());
  statusDot(CX + 22, 218, "RTC", rtcPresent());
  statusDot(CX - 110, 246, "PMU", hwPmuOk());
  statusDot(CX + 22, 246, "PSRAM", ESP.getPsramSize() > 0);
  statusDot(CX - 110, 274, "BLE", bleConnected());  // verde = connesso e associato
  statusDot(CX + 22, 274, "USB", usbPresent());
  snprintf(buf, sizeof(buf), "RAM %uK PSRAM %uK", (unsigned)(ESP.getFreeHeap() / 1024), (unsigned)(ESP.getFreePsram() / 1024));
  centerText(buf, 310, 2, RGB565(130, 140, 165));
  int bp = batPercent();
  if (bp >= 0) {
    snprintf(buf, sizeof(buf), "Batt %d%%%s", bp, batCharging() ? " carica" : usbPresent() ? " USB" : "");
    centerText(buf, 336, 2, RGB565(170, 175, 195));
  }
  centerText(__DATE__, 366, 1, RGB565(90, 100, 120));
}

// ---- Menu impostazioni (si apre tenendo premuto BOOT) ----
int setFocus = 0;  // controllo evidenziato (tasti fisici): 0 volume, 1 luminosita', 2 standby, 3 associa, 4 dimentica, 5 spegni, 6 esci
uint32_t setLastInput = 0;
uint32_t btnFlashUntil = 0;  // feedback visivo del bottone premuto
int btnFlashId = -1;
const int SET_FOCUS_N = 7;
static const uint16_t STANDBY_STEPS[] = { 0, 1, 2, 5, 10, 15, 30, 60 };

void saveBrightness() {
  Preferences pf;
  pf.begin("claudebuddy", false);
  pf.putUChar("bri", userBrightness);
  pf.end();
}

void saveAutoStandby() {
  Preferences pf;
  pf.begin("claudebuddy", false);
  pf.putUShort("asb", autoStandbyMin);
  pf.end();
}

int standbyIndex() {
  int n = sizeof(STANDBY_STEPS) / sizeof(STANDBY_STEPS[0]);
  for (int i = 0; i < n; i++)
    if (STANDBY_STEPS[i] == autoStandbyMin) return i;
  return 0;
}

void openSettings() {
  settingsOpen = true;
  setFocus = 0;
  setLastInput = millis();
  lastTouch = setLastInput;
  sfxPlay(SFX_TAP);
}

void closeSettings() {
  settingsOpen = false;
  lastTouch = millis();
  sfxPlay(SFX_TAP);
}

// azioni dei controlli; d = +1/-1 per i valori, wrap = giro completo (tasti fisici)
void actVolume(int d, bool wrap) {
  int v = audioVolume() + d * 10;
  if (v > 100) v = wrap ? 0 : 100;
  if (v < 0) v = 0;
  audioSetVolume(v);
  sfxPlay(SFX_TAP);
}
void actBright(int d, bool wrap) {
  int v = userBrightness + d * 25;
  if (v > 255) v = wrap ? 20 : 255;
  if (v < 20) v = 20;
  userBrightness = (uint8_t)v;
  saveBrightness();
}
void actStandby(int d, bool wrap) {
  int n = sizeof(STANDBY_STEPS) / sizeof(STANDBY_STEPS[0]);
  int idx = standbyIndex() + d;
  if (idx >= n) idx = wrap ? 0 : n - 1;
  if (idx < 0) idx = 0;
  autoStandbyMin = STANDBY_STEPS[idx];
  saveAutoStandby();
  sfxPlay(SFX_TAP);
}
void actPair() {
  if (blePairingOpen(nullptr)) bleSetPairing(false);
  else {
    bleSetPairing(true);
    settingsOpen = false;  // si passa alla schermata di associazione
  }
  sfxPlay(SFX_TAP);
}
void actUnpair() {
  bleUnpairAll();
  showToast("PC dimenticati");
  sfxPlay(SFX_DONE);
}
void actOff() {
  settingsOpen = false;
  powerOff();
}

// tasti fisici: attiva il controllo evidenziato
void settingsActivate() {
  setLastInput = millis();
  lastTouch = setLastInput;
  switch (setFocus) {
    case 0: actVolume(1, true); break;
    case 1: actBright(1, true); break;
    case 2: actStandby(1, true); break;
    case 3: actPair(); break;
    case 4: actUnpair(); break;
    case 5: actOff(); break;
    default: closeSettings(); break;
  }
}

// geometria
static const int ST_ROW_Y[3] = { 66, 126, 186 };
const int ST_ROW_H = 52;
const int ST_MINUS_X = -4, ST_PLUS_X = 106, ST_STEP_W = 46;  // relativi a CX
static const int ST_BTN_Y[2] = { 258, 322 };
const int ST_BTN_H = 56;

static bool inRect(int px, int py, int x, int y, int w, int h, int pad = 6) {
  return px >= x - pad && px <= x + w + pad && py >= y - pad && py <= y + h + pad;
}

void settingsTouch(int sx, int sy) {
  setLastInput = millis();
  lastTouch = setLastInput;
  // righe con - / +
  for (int r = 0; r < 3; r++) {
    int y = ST_ROW_Y[r];
    int dir = 0;
    if (inRect(sx, sy, CX + ST_MINUS_X, y, ST_STEP_W, ST_ROW_H)) dir = -1;
    else if (inRect(sx, sy, CX + ST_PLUS_X, y, ST_STEP_W, ST_ROW_H)) dir = 1;
    if (dir) {
      setFocus = r;
      btnFlashId = r * 2 + (dir > 0);
      btnFlashUntil = millis() + 150;
      if (r == 0) actVolume(dir, false);
      else if (r == 1) actBright(dir, false);
      else actStandby(dir, false);
      return;
    }
  }
  // bottoni 2x2
  for (int b = 0; b < 4; b++) {
    int x = CX + ((b & 1) ? 6 : -150), y = ST_BTN_Y[b >> 1];
    if (inRect(sx, sy, x, y, 144, ST_BTN_H)) {
      setFocus = 3 + b;
      btnFlashId = 6 + b;
      btnFlashUntil = millis() + 150;
      settingsActivate();
      return;
    }
  }
  if (sy > 395) closeSettings();  // fascia in basso = chiudi
}

// bottone arrotondato con etichetta centrata
static void drawButton(int x, int y, int w, int h, const char *label, uint16_t fill, uint16_t txt, bool focus, bool flash, uint8_t size = 2) {
  g->fillRoundRect(x, y, w, h, 14, flash ? RGB565(90, 100, 135) : fill);
  if (focus) g->drawRoundRect(x - 2, y - 2, w + 4, h + 4, 16, RGB565(255, 220, 120));
  g->setTextSize(size);
  g->setTextColor(txt);
  int16_t x1, y1;
  uint16_t tw, th;
  g->getTextBounds(label, 0, 0, &x1, &y1, &tw, &th);
  g->setCursor(x + (w - (int)tw) / 2, y + (h - (int)th) / 2);
  g->print(label);
}

void drawSettingsPage() {
  g->fillScreen(RGB565_BLACK);
  centerText("Impostazioni", 24, 2, RGB565(200, 205, 220));
  uint32_t t = millis();
  bool fl = (int32_t)(btnFlashUntil - t) > 0;
  const uint16_t bg = RGB565(36, 42, 62), bgStep = RGB565(48, 56, 82);
  char v[16];
  const char *labels[3] = { "Volume", "Luminosita'", "Standby" };
  for (int r = 0; r < 3; r++) {
    int y = ST_ROW_Y[r];
    bool foc = setFocus == r;
    g->fillRoundRect(CX - 150, y, 300, ST_ROW_H, 14, RGB565(20, 24, 36));
    if (foc) g->drawRoundRect(CX - 152, y - 2, 304, ST_ROW_H + 4, 16, RGB565(255, 220, 120));
    g->setTextSize(2);
    g->setTextColor(RGB565(170, 178, 200));
    g->setCursor(CX - 140, y + (ST_ROW_H - 14) / 2);
    g->print(labels[r]);
    if (r == 0) {
      if (audioVolume() == 0) strcpy(v, "muto");
      else snprintf(v, sizeof(v), "%u%%", (unsigned)audioVolume());
    } else if (r == 1) {
      snprintf(v, sizeof(v), "%d%%", (int)userBrightness * 100 / 255);
    } else {
      if (autoStandbyMin == 0) strcpy(v, "mai");
      else snprintf(v, sizeof(v), "%um", (unsigned)autoStandbyMin);
    }
    drawButton(CX + ST_MINUS_X, y + 3, ST_STEP_W, ST_ROW_H - 6, "-", bgStep, RGB565(255, 220, 120), false, fl && btnFlashId == r * 2, 3);
    drawButton(CX + ST_PLUS_X, y + 3, ST_STEP_W, ST_ROW_H - 6, "+", bgStep, RGB565(255, 220, 120), false, fl && btnFlashId == r * 2 + 1, 3);
    centerTextAt(v, CX + 70 + 0, y + (ST_ROW_H - 14) / 2, 2, RGB565(235, 238, 248));
  }
  bool pairOpen = blePairingOpen(nullptr);
  drawButton(CX - 150, ST_BTN_Y[0], 144, ST_BTN_H, pairOpen ? "Annulla" : "Associa PC", pairOpen ? RGB565(70, 90, 50) : RGB565(34, 70, 110), RGB565(235, 238, 248), setFocus == 3, fl && btnFlashId == 6);
  drawButton(CX + 6, ST_BTN_Y[0], 144, ST_BTN_H, "Dimentica", bg, RGB565(235, 238, 248), setFocus == 4, fl && btnFlashId == 7);
  drawButton(CX - 150, ST_BTN_Y[1], 144, ST_BTN_H, "Spegni", RGB565(100, 36, 36), RGB565(255, 200, 190), setFocus == 5, fl && btnFlashId == 8);
  drawButton(CX + 6, ST_BTN_Y[1], 144, ST_BTN_H, "Esci", bg, RGB565(235, 238, 248), setFocus == 6, fl && btnFlashId == 9);
  if ((int32_t)(toastUntil - t) > 0) centerText(toast, 400, 2, RGB565(255, 230, 140));
  else centerText("BOOT: scorri  POWER: ok", 408, 1, RGB565(110, 120, 140));
  if (canvas) canvas->flush();
}

// ---- Schermata di associazione: attesa del PC (il codice compare dopo) ----
void drawPairWaitPage() {
  g->fillScreen(RGB565_BLACK);
  uint32_t left = 0;
  blePairingOpen(&left);
  centerText("Associazione", 70, 3, RGB565(200, 205, 220));
  centerText("Sul PC apri", 130, 2, RGB565(150, 160, 185));
  centerText("Bluetooth > Aggiungi", 160, 2, RGB565(235, 238, 248));
  centerText("dispositivo", 186, 2, RGB565(235, 238, 248));
  centerText("e scegli", 220, 2, RGB565(150, 160, 185));
  centerText("Claude Buddy", 250, 3, RGB565(255, 200, 90));
  char b[12];
  snprintf(b, sizeof(b), "%u:%02u", (unsigned)(left / 60000), (unsigned)((left / 1000) % 60));
  centerText(b, 306, 3, RGB565(120, 130, 150));
  drawButton(CX - 80, 354, 160, 52, "Annulla", RGB565(60, 40, 40), RGB565(255, 200, 190), false, false);
  if (canvas) canvas->flush();
}

// ---- Associazione BLE: codice da digitare sul PC ----
void drawPairingPage(uint32_t code) {
  g->fillScreen(RGB565_BLACK);
  centerText("Associazione", 90, 3, RGB565(200, 205, 220));
  centerText("Digita questo codice", 150, 2, RGB565(130, 140, 165));
  centerText("sul PC", 176, 2, RGB565(130, 140, 165));
  char buf[10];
  snprintf(buf, sizeof(buf), "%06u", (unsigned)code);
  centerText(buf, 240, 6, RGB565(255, 200, 90));
  centerText("Bluetooth > Aggiungi dispositivo", 350, 1, RGB565(90, 100, 120));
  if (canvas) canvas->flush();
}

// ---- Cornice: pagina corrente + indicatore ----
void drawFrame() {
  uint32_t pk;
  if (blePasskeyActive(&pk)) {
    drawPairingPage(pk);
    return;
  }
  if (blePairingOpen(nullptr)) {
    drawPairWaitPage();
    return;
  }
  if (settingsOpen) {
    drawSettingsPage();
    return;
  }
  C_CLAWD = (COLORS[styleColor].lvl <= petLevel) ? COLORS[styleColor].c : COLORS[0].c;
  if ((int32_t)(bootEnd - millis()) > 0) {  // avvio: solo l'animazione
    drawBoot();
    if (canvas) canvas->flush();
    return;
  }
  switch (page) {
    case PG_SESSIONS: drawSessionsPage(); break;
    case PG_USAGE: drawUsagePage(); break;
    case PG_TODAY: drawTodayPage(); break;
    case PG_TIMER: drawTimerPage(); break;
    case PG_MSG: drawMsgPage(); break;
    case PG_STYLE: drawStylePage(); break;
    case PG_INFO: drawInfoPage(); break;
    default: drawClawdPage(); break;
  }
  // puntini di pagina
  for (int i = 0; i < PG_COUNT; i++)
    g->fillCircle(CX + (int)((i - (PG_COUNT - 1) / 2.0f) * 18), 440, i == page ? 4 : 2, i == page ? RGB565(210, 215, 230) : RGB565(70, 75, 90));
  if (canvas) canvas->flush();
}

bool startsWithCI(const char *s, const char *prefix) {
  while (*prefix) {
    if (tolower((unsigned char)*s) != tolower((unsigned char)*prefix)) return false;
    s++;
    prefix++;
  }
  return true;
}

const char *argOf(const char *line, size_t cmdLen) {
  const char *p = line + cmdLen;
  while (*p == ' ' || *p == ':' || *p == '=') p++;
  return p;
}

static const char *takeToken(const char *p, char *out, size_t n) {
  size_t k = 0;
  while (*p && *p != ' ') {
    if (k < n - 1) out[k++] = *p;
    p++;
  }
  out[k] = 0;
  while (*p == ' ') p++;
  return p;
}

// "sess <i> <S> <ctx> <cache_s> <modello> <effort> <fast> <nome>|<tool>"  (argomenti dopo "sess")
void parseSess(const char *p) {
  int i = atoi(p);
  while (*p && *p != ' ') p++;
  while (*p == ' ') p++;
  char st = *p;
  if (*p) p++;
  while (*p == ' ') p++;
  int ctx = atoi(p);
  while (*p && *p != ' ') p++;
  while (*p == ' ') p++;
  long cache = atol(p);
  while (*p && *p != ' ') p++;
  while (*p == ' ') p++;
  char model[12], effort[10], fastTok[4];
  p = takeToken(p, model, sizeof(model));
  p = takeToken(p, effort, sizeof(effort));
  p = takeToken(p, fastTok, sizeof(fastTok));
  if (i < 0 || i >= MAX_SESS) return;
  Sess &s = sess[i];
  s.st = st;
  s.ctx = (int8_t)constrain(ctx, -1, 127);
  s.cacheS = cache;
  s.cacheAt = millis();
  for (char *q = model; *q; q++)
    if (*q == '_') *q = ' ';
  strncpy(s.model, strcmp(model, "-") ? model : "", sizeof(s.model) - 1);
  s.model[sizeof(s.model) - 1] = 0;
  strncpy(s.effort, strcmp(effort, "-") ? effort : "", sizeof(s.effort) - 1);
  s.effort[sizeof(s.effort) - 1] = 0;
  s.fast = fastTok[0] == 'F';
  const char *bar = strchr(p, '|');
  size_t nl = bar ? (size_t)(bar - p) : strlen(p);
  if (nl > sizeof(s.name) - 1) nl = sizeof(s.name) - 1;
  memcpy(s.name, p, nl);
  s.name[nl] = 0;
  strncpy(s.tool, bar ? bar + 1 : "", sizeof(s.tool) - 1);
  s.tool[sizeof(s.tool) - 1] = 0;
}

// esperienza -> livello: lv = 1 + floor(sqrt(xp / 25)); accessori a lv 3 (cappellino), 6 (occhiali), 10 (corona)
void setPetXp(long xp) {
  if (xp < 0) xp = 0;
  int lv = 1 + (int)sqrtf(xp / 25.0f);
  long lo = 25L * (lv - 1) * (lv - 1), hi = 25L * lv * lv;
  petProg = (float)(xp - lo) / (float)(hi - lo);
  if (petKnown && lv > petLevel) {
    lvlUpUntil = millis() + 3500;
    sfxPlay(SFX_LEVEL);
  }
  petLevel = lv;
  petKnown = true;
}

// ora a muro dal PC (secondi dal 1970 in ora locale): imposta orologio di sistema e RTC
void setClockEpoch(uint32_t e) {
  struct timeval tv = { (time_t)e, 0 };
  settimeofday(&tv, nullptr);
  if (rtcPresent()) {
    uint32_t r = rtcGet();
    if (r == 0 || abs((int32_t)(r - e)) > 30) rtcSet(e);
  }
  clockValid = true;
}

// "msg <eta_s> <sessione>|<testo>"  (argomenti dopo "msg")
void parseMsg(const char *p) {
  uint32_t age = (uint32_t)atol(p);
  while (*p && *p != ' ') p++;
  while (*p == ' ') p++;
  const char *bar = strchr(p, '|');
  if (!bar) return;
  const char *text = bar + 1;
  if (strncmp(text, msgText, sizeof(msgText) - 1) != 0 || strlen(text) != strlen(msgText)) {
    msgScroll = 0;
    if (msgText[0] && age < 300) msgUnread = true;  // messaggio nuovo (non quello di prima di un riavvio)
  }
  size_t nl = bar - p;
  if (nl > sizeof(msgName) - 1) nl = sizeof(msgName) - 1;
  memcpy(msgName, p, nl);
  msgName[nl] = 0;
  strncpy(msgText, text, sizeof(msgText) - 1);
  msgText[sizeof(msgText) - 1] = 0;
  msgAgeS = age;
  msgRecvAt = millis();
}

void handleLine(const char *line) {
  while (*line == ' ') line++;
  if (!*line) return;
  if (demoActive && (startsWithCI(line, "idle") || startsWithCI(line, "thinking") || startsWithCI(line, "working") ||
                     startsWithCI(line, "attention") || startsWithCI(line, "done") || startsWithCI(line, "sleep") ||
                     startsWithCI(line, "error") || startsWithCI(line, "usage") || startsWithCI(line, "fx")))
    return;  // durante la demo il PC non cambia la scena
  if (startsWithCI(line, "touchdebug")) touchDebug = atoi(argOf(line, 10)) != 0;
  else if (startsWithCI(line, "unpair")) bleUnpairAll();
  else if (startsWithCI(line, "standby")) enterStandby();
  else if (startsWithCI(line, "wake")) exitStandby();
  else if (startsWithCI(line, "off")) powerOff();
  else if (startsWithCI(line, "autostandby")) {
    autoStandbyMin = (uint16_t)constrain(atoi(argOf(line, 11)), 0, 600);
    Preferences pf;
    pf.begin("claudebuddy", false);
    pf.putUShort("asb", autoStandbyMin);
    pf.end();
  } else if (startsWithCI(line, "idle")) setState(S_IDLE, argOf(line, 4));
  else if (startsWithCI(line, "thinking")) setState(S_THINKING, argOf(line, 8));
  else if (startsWithCI(line, "working")) setState(S_WORKING, argOf(line, 7));
  else if (startsWithCI(line, "attention")) setState(S_ATTENTION, argOf(line, 9));
  else if (startsWithCI(line, "done")) setState(S_DONE, argOf(line, 4));
  else if (startsWithCI(line, "sleep")) setState(S_SLEEP, "");
  else if (startsWithCI(line, "error")) setState(S_ERROR, "");
  else if (startsWithCI(line, "agents")) agentsN = constrain(atoi(argOf(line, 6)), 0, 4);
  else if (startsWithCI(line, "pet")) setPetXp(atol(argOf(line, 3)));
  else if (startsWithCI(line, "stats")) sscanf(argOf(line, 5), "%d %d %d", &statCents, &statAdd, &statRm);
  else if (startsWithCI(line, "epoch")) setClockEpoch((uint32_t)strtoul(argOf(line, 5), nullptr, 10));
  else if (startsWithCI(line, "sound")) audioSetEnabled(atoi(argOf(line, 5)) != 0);
  else if (startsWithCI(line, "todo")) sscanf(argOf(line, 4), "%d %d", &todoDone, &todoTotal);
  else if (startsWithCI(line, "msg")) parseMsg(argOf(line, 3));
  else if (startsWithCI(line, "today")) sscanf(argOf(line, 5), "%d %d %d", &todayPrompts, &todayTools, &todayMin);
  else if (startsWithCI(line, "fx")) {
    long seq;
    int age;
    char nm[16];
    if (sscanf(argOf(line, 2), "%ld %d %15s", &seq, &age, nm) == 3 && seq != fxSeq) {
      fxSeq = seq;
      if (age <= 10) {
        uint8_t k = !strcmp(nm, "commit") ? FX_COMMIT : !strcmp(nm, "push") ? FX_PUSH : !strcmp(nm, "testok") ? FX_TESTOK
                  : !strcmp(nm, "testfail") ? FX_TESTFAIL : !strcmp(nm, "compact") ? FX_COMPACT : FX_NONE;
        if (k) startFx(k);
      }
    }
  } else if (startsWithCI(line, "tick")) {
    strncpy(ticker, argOf(line, 4), sizeof(ticker) - 1);
    ticker[sizeof(ticker) - 1] = 0;
  }
  else if (startsWithCI(line, "sessions")) sessN = constrain(atoi(argOf(line, 8)), 0, MAX_SESS);
  else if (startsWithCI(line, "sess")) parseSess(argOf(line, 4));
  else if (startsWithCI(line, "usage")) {
    int a, b, c, d, e;
    if (sscanf(argOf(line, 5), "%d %d %d %d %d", &a, &b, &c, &d, &e) == 5) {
      int prev5 = u5, prev7 = u7;
      u5 = a; u5min = b; u7 = c; u7min = d; uctx = e;
      usageAt = millis();
      // allarme: il limite a 5 ore passa il 90% (o quello settimanale il 95%)
      bool cross5 = prev5 >= 0 && prev5 < 90 && a >= 90;
      bool cross7 = prev7 >= 0 && prev7 < 95 && c >= 95;
      if (cross5 || cross7) {
        alarmPct = cross5 ? a : c;
        alarmUntil = millis() + 4000;
        page = PG_CLAWD;
        lastTouch = millis();
        sfxPlay(SFX_ALERT);
        if (state == S_SLEEP) setState(S_IDLE, "");
      } else if (prev5 >= 85 && a >= 0 && a < 50) {  // la finestra si e' azzerata
        resetUntil = millis() + 4500;
        page = PG_CLAWD;
        lastTouch = millis();
        sfxPlay(SFX_LEVEL);
        if (state == S_SLEEP) setState(S_IDLE, "");
      }
    }
  } else if (startsWithCI(line, "time")) {
    if (!clockValid) {  // senza ora di sistema vale quella grezza del PC
      const char *a = argOf(line, 4);
      strncpy(clockText, a, 5);
      clockText[5] = 0;
    }
  } else if (startsWithCI(line, "bright")) {
    int v = atoi(argOf(line, 6));
    userBrightness = constrain(v, 0, 255);
  }
}

void feedLine(LineIn &in, char c) {
  if (c == '\n' || c == '\r') {
    if (in.len) {
      in.buf[in.len] = 0;
      if (touchDebug) Serial.printf("L %.24s\n", in.buf);
      handleLine(in.buf);
      if (touchDebug) Serial.printf("  page=%d\n", (int)page);
      lastDataAt = millis();
      in.len = 0;
    }
  } else if (in.len < sizeof(in.buf) - 1) {
    in.buf[in.len++] = c;
  } else {
    in.len = 0;  // riga troppo lunga: scarta
  }
}

void pollSerial() {
  while (Serial.available()) feedLine(serIn, (char)Serial.read());
  int b;
  for (int n = 0; n < 1024 && (b = bleRead()) >= 0; n++) feedLine(bleIn, (char)b);
}

// ---- Tasto BOOT: demo degli stati ----
// ---- Demo animazioni: BOOT breve le scorre tutte ----
static const char *const DEMO_NAMES[] = {
  "In attesa", "Penso", "Lavoro", "Errore", "Serve te", "Fatto", "Dorme", "Annoiato", "Stanco", "Memoria piena",
  "Cache fredda", "Batt. scarica", "Allarme batt.", "Carica completa", "Commit", "Push", "Test ok", "Compattazione",
  "Livello su", "Allarme usage", "Limite azzerato", "Carezza", "Risata", "Infastidito", "Stordito", "Mezzanotte"
};
const int DEMO_N = sizeof(DEMO_NAMES) / sizeof(DEMO_NAMES[0]);
// ripetizione automatica (ms) per gli effetti a tempo, 0 = resta finche' non premi
static const uint16_t DEMO_PERIOD[DEMO_N] = {
  0, 0, 0, 2800, 0, 4500, 0, 0, 0, 0,
  0, 0, 6500, 7500, 4500, 4800, 4500, 4500,
  5000, 5500, 6000, 3200, 3500, 4000, 4500, 8500
};

void demoApply(int i) {
  uint32_t now = millis();
  dizzyUntil = lvlUpUntil = alarmUntil = resetUntil = 0;
  annoyUntil = laughUntil = midnightUntil = petUntil = 0;
  batAlertUntil = batFullUntil = 0;
  fxUntil = 0;
  fxKind = FX_NONE;
  demoKind = DM_NONE;
  page = PG_CLAWD;
  lastTouch = now;
  lastEvent = now;
  setState(S_IDLE, "");
  switch (i) {
    case 1: setState(S_THINKING, ""); break;
    case 2: setState(S_WORKING, "Bash"); break;
    case 3: setState(S_ERROR, ""); break;
    case 4: setState(S_ATTENTION, "Permesso: Bash"); break;
    case 5: setState(S_DONE, ""); break;
    case 6: setState(S_SLEEP, ""); break;
    case 7: demoKind = DM_BORED; break;
    case 8: demoKind = DM_TIRED; break;
    case 9: demoKind = DM_FULL; break;
    case 10: demoKind = DM_COLD; break;
    case 11: demoKind = DM_LOWBAT; break;
    case 12: batAlertUntil = now + 5000; sfxPlay(SFX_ERROR); break;
    case 13: batFullUntil = now + 6000; sfxPlay(SFX_DONE); break;
    case 14: startFx(FX_COMMIT); break;
    case 15: startFx(FX_PUSH); break;
    case 16: startFx(FX_TESTOK); break;
    case 17: startFx(FX_COMPACT); break;
    case 18: lvlUpUntil = now + 3500; sfxPlay(SFX_LEVEL); break;
    case 19: alarmPct = 92; alarmUntil = now + 4000; sfxPlay(SFX_ALERT); break;
    case 20: resetUntil = now + 4500; sfxPlay(SFX_LEVEL); break;
    case 21: petUntil = now + 1600; sfxPlay(SFX_HEART); break;
    case 22: laughUntil = now + 2000; sfxPlay(SFX_HEART); break;
    case 23: annoyUntil = now + 2500; sfxPlay(SFX_ERROR); break;
    case 24: dizzyUntil = now + 3000; sfxPlay(SFX_DIZZY); break;
    case 25: midnightUntil = now + 7000; sfxPlay(SFX_HEART); break;
    default: break;
  }
  char b[24];
  snprintf(b, sizeof(b), "%d/%d %s", i + 1, DEMO_N, DEMO_NAMES[i]);
  showToast(b);
  demoRetrig = now + (DEMO_PERIOD[i] ? DEMO_PERIOD[i] : 0x7fffffff);
}

void demoNext() {
  demoActive = true;
  demoIdx = (demoIdx + 1) % DEMO_N;
  demoEnds = millis() + 60000;
  demoApply(demoIdx);
}

// da chiamare nel loop: ripete gli effetti a tempo e chiude la demo dopo un minuto di inattivita'
void demoService(uint32_t now) {
  if (!demoActive) return;
  if ((int32_t)(now - demoEnds) > 0) {
    demoActive = false;
    demoKind = DM_NONE;
    return;
  }
  if (demoRetrig != 0x7fffffffu && (int32_t)(now - demoRetrig) >= 0) demoApply(demoIdx);
  else lastTouch = now;  // niente standby/riposo durante la demo
}

void pollButton() {
  static bool prev = true;
  static uint32_t lastPress = 0;
  static uint32_t downAt = 0;
  static bool longDone = false;
  bool now = digitalRead(BOOT_PIN);
  uint32_t ms = millis();
  if (prev && !now) {
    downAt = ms;
    longDone = false;
  }
  if (!now && !longDone && ms - downAt >= 700) {  // pressione lunga: menu impostazioni
    longDone = true;
    if (standby) exitStandby();
    else if (settingsOpen) closeSettings();
    else openSettings();
  }
  bool released = now && !prev && !longDone && ms - downAt > 40;
  if (released && ms - lastPress > 250) {
    lastPress = ms;
    if (standby) {
      exitStandby();
      prev = now;
      return;
    }
    if (blePairingOpen(nullptr)) {  // BOOT annulla l'associazione
      bleSetPairing(false);
      prev = now;
      return;
    }
    if (settingsOpen) {
      setFocus = (setFocus + 1) % SET_FOCUS_N;
      setLastInput = ms;
      lastTouch = ms;
      sfxPlay(SFX_TAP);
      prev = now;
      return;
    }
    demoNext();  // BOOT breve: prossima animazione di Clawd
    lastTouch = millis();
  }
  prev = now;
}

// ---- Touch: swipe = cambia pagina, tap su Clawd = carezza ----
#define SWIPE_INVERT 0  // 1 se lo swipe va nel verso sbagliato

void IRAM_ATTR touchIsr() { gTouchIrq = true; }

void setupTouch() {
  Wire.begin(IIC_SDA, IIC_SCL);
  Wire.setTimeOut(50);  // timeout del bus
  Wire.setTimeout(20);  // timeout di readBytes (default 1 s: bloccherebbe il loop)
  touch.setPins(TP_RESET, TP_INT);
  for (int i = 0; i < 3 && !touchOk; i++) {
    touchOk = touch.begin(Wire, TOUCH_ADDR, IIC_SDA, IIC_SCL);
    if (!touchOk) delay(150);
  }
  if (!touchOk) return;
  touch.reset();  // begin() lascia il chip in modo comando
  touch.setMaxCoordinates(LCD_WIDTH, LCD_HEIGHT);
  touch.setMirrorXY(true, true);
  pinMode(TP_INT, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(TP_INT), touchIsr, FALLING);
}

void showToast(const char *msg) {
  strncpy(toast, msg, sizeof(toast) - 1);
  toast[sizeof(toast) - 1] = 0;
  toastUntil = millis() + 1800;
}

void cycleColor() {
  for (int i = 1; i <= N_COLORS; i++) {
    int c = (styleColor + i) % N_COLORS;
    if (COLORS[c].lvl <= petLevel) { styleColor = c; break; }
  }
}
void cycleHat() {
  for (int i = 1; i <= 4; i++) {
    int h = (styleHat + i) % 4;
    if (HAT_LVL[h] <= petLevel) { styleHat = h; break; }
  }
}
void cycleGlasses() {
  for (int i = 1; i <= 3; i++) {
    int v = (styleGlasses + i) % 3;
    if (GLS_LVL[v] <= petLevel) { styleGlasses = v; break; }
  }
}

// evento una tantum dal PC; per il test fallito basta l'avviso (la faccia di errore la da' lo stato)
void startFx(uint8_t kind) {
  uint32_t now = millis();
  if (kind == FX_TESTFAIL) {
    showToast("Test falliti");
    return;
  }
  fxKind = kind;
  fxStart = now;
  fxUntil = now + (kind == FX_PUSH ? 3300 : 3000);
  sfxPlay(kind == FX_PUSH ? SFX_LEVEL : kind == FX_COMPACT ? SFX_TAP : kind == FX_COMMIT ? SFX_HEART : SFX_DONE);
  if (state == S_SLEEP) setState(S_IDLE, "");
  if (ago(now, lastTouch) > 15000) page = PG_CLAWD;  // se stai leggendo qualcosa non ti sposta
}

void onGesture(int dx, int dy, uint32_t dur, int sx, int sy) {
  uint32_t now = millis();
  if (touchDebug) Serial.printf("G dx=%d dy=%d dur=%u sx=%d sy=%d page=%d\n", dx, dy, (unsigned)dur, sx, sy, (int)page);
  if (blePairingOpen(nullptr)) {  // schermata di associazione: un tocco annulla
    if (abs(dx) < 30 && abs(dy) < 30) bleSetPairing(false);
    return;
  }
  if (settingsOpen) {
    if (abs(dx) < 30 && abs(dy) < 30) settingsTouch(sx, sy);
    return;
  }
  if (abs(dx) > 60 && abs(dx) > abs(dy) * 3 / 2) {
    bool next = (dx < 0) != (SWIPE_INVERT != 0);
    page = (page + (next ? 1 : PG_COUNT - 1)) % PG_COUNT;
    if (touchDebug) Serial.printf("PAGE %d dx=%d dy=%d\n", (int)page, dx, dy);
    sfxPlay(SFX_TAP);
  } else if (page == PG_MSG && abs(dy) > 50 && abs(dy) > abs(dx) * 3 / 2) {
    msgScroll += (dy < 0) ? 5 : -5;  // trascina verso l'alto = scorre avanti (il limite lo applica il disegno)
    if (msgScroll < 0) msgScroll = 0;
    sfxPlay(SFX_TAP);
  } else if (abs(dx) < 25 && abs(dy) < 25) {
    bool longPress = dur >= 700;
    if (page == PG_CLAWD) {
      if (longPress) {  // tenere premuto su Clawd: apre lo Stile
        page = PG_STYLE;
        sfxPlay(SFX_TAP);
      } else if (dur < 600) {
        if (state == S_SLEEP) setState(S_IDLE, "");  // il tocco lo sveglia
        // tocchi ripetuti entro 1,2 s: 3+ ride, 6+ si infastidisce
        tapCount = (now - lastTapAt > 1200) ? 1 : tapCount + 1;
        lastTapAt = now;
        if (tapCount >= 6) {
          annoyUntil = now + 2500;
          tapCount = 0;
          sfxPlay(SFX_ERROR);
        } else if (tapCount >= 3) {
          laughUntil = now + 2000;
          sfxPlay(SFX_HEART);
        } else if (state == S_IDLE || state == S_DONE) {
          petUntil = now + 1600;
          sfxPlay(SFX_HEART);
        }
      }
    } else if (page == PG_STYLE) {
      for (int i = 0; i < 4; i++) {
        if (inRect(sx, sy, CX + STY_X[i & 1], STY_Y[i >> 1], STY_W, STY_H)) {
          if (i == 0) cycleColor();
          else if (i == 1) cycleHat();
          else if (i == 2) cycleGlasses();
          else audioSetEnabled(!audioEnabled());
          styFlashId = i;
          styFlashUntil = millis() + 150;
          saveStyle();
          sfxPlay(SFX_TAP);
          break;
        }
      }
    } else if (page == PG_TIMER) {
      if (longPress) {  // reset
        tmRunning = false;
        tmFocus = true;
        tmLeftMs = TM_FOCUS_MS;
      } else {
        tmRunning = !tmRunning;
        tmLast = now;
      }
      sfxPlay(SFX_TAP);
    }
  }
}

void pollTouch() {
  static uint32_t lastPoll = 0, t0 = 0;
  static bool wasPressed = false;
  static int16_t x0, y0, xl, yl;
  if (!touchOk) return;
  uint32_t now = millis();
  if (now - lastPoll < 20) return;
  lastPoll = now;
  // si legge il bus solo se il chip ha avvisato (INT) o se il dito e' ancora giu':
  // leggere il CST9217 dormiente bloccava il loop per ~1 s
  if (!gTouchIrq && !wasPressed) return;
  gTouchIrq = false;
  Wire.beginTransmission(TOUCH_ADDR);  // ciclo di solo indirizzo: sveglia il chip
  if (Wire.endTransmission() != 0) return;
  int16_t x, y;
  bool pressed = touch.getPoint(&x, &y, 1) > 0;
  if (touchDebug) {
    if (pressed) Serial.printf("T %u %d %d\n", (unsigned)now, (int)x, (int)y);
    else Serial.printf("U %u was=%d dx=%d dy=%d\n", (unsigned)now, (int)wasPressed, (int)(xl - x0), (int)(yl - y0));
  }
  if (pressed) {
    lastTouch = now;
    if (!wasPressed) { x0 = xl = x; y0 = yl = y; t0 = now; }
    else { xl = x; yl = y; }
  } else if (wasPressed) {
    onGesture(xl - x0, yl - y0, now - t0, x0, y0);
  }
  wasPressed = pressed;
}

// ---- Setup / loop ----
void setup() {
  Serial.setRxBufferSize(2048);  // lo snapshot dal PC arriva in blocco (~400 byte)
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);      // niente blocchi se nessuno ascolta
  pinMode(BOOT_PIN, INPUT_PULLUP);
  bleBegin();

  if (!panel->begin()) {
    // senza schermo non c'e' molto da fare: continua comunque a leggere la seriale
  }
  panel->fillScreen(RGB565_BLACK);
  panel->setBrightness(0);

  canvas = new Arduino_Canvas(LCD_WIDTH, LCD_HEIGHT, panel, 0, 0);
  if (canvas->begin(GFX_SKIP_OUTPUT_BEGIN)) {
    g = canvas;
  } else {
    delete canvas;
    canvas = nullptr;
    g = panel;  // fallback: disegno diretto (puo' sfarfallare)
  }

  setupTouch();
  hwBegin();
  {
    Preferences pf;
    pf.begin("claudebuddy", true);
    styleColor = pf.getUChar("col", 0) % N_COLORS;
    styleHat = pf.getUChar("hat", 3) % 4;
    styleGlasses = pf.getUChar("gls", 2) % 3;
    autoStandbyMin = pf.getUShort("asb", 10);
    userBrightness = pf.getUChar("bri", 120);
    pf.end();
  }
  uint32_t r = rtcGet();  // ora dell'RTC (resta giusta anche a PC spento)
  if (r) {
    struct timeval tv = { (time_t)r, 0 };
    settimeofday(&tv, nullptr);
    clockValid = true;
  }
  setState(S_IDLE, "");
  bootStart = millis();
  bootEnd = bootStart + 3300;
  sfxPlay(SFX_LEVEL);
}

void loop() {
  static uint32_t lastFrame = 0;
  uint32_t now = millis();

  pollSerial();
  pollButton();
  bleService();

  // tasto POWER: breve = standby / risveglio, lungo = spegnimento
  PwrKey pk = pwrKeyPoll(now);
  if (pk == PWR_LONG) powerOff();
  else if (pk == PWR_SHORT) {
    if (standby) exitStandby();
    else if (settingsOpen) settingsActivate();
    else if (blePairingOpen(nullptr)) bleSetPairing(false);
    else enterStandby();
  }
  if (settingsOpen && millis() - setLastInput > 30000) settingsOpen = false;  // si chiude da solo

  if (standby) {
    // schermo spento: si resta in ascolto; serve-te sveglia lo schermo
    if (state == S_ATTENTION || blePasskeyActive(nullptr)) exitStandby();
    if (tmRunning) tmLast = now;  // il timer resta in pausa
    batUpdate(now);
    delay(30);
    return;
  }

  pollTouch();

  // scossa: Clawd si stordisce (e si sveglia)
  if (imuShaken(now)) {
    dizzyUntil = now + 3000;
    lastTouch = now;
    sfxPlay(SFX_DIZZY);
    if (state == S_SLEEP) setState(S_IDLE, "");
  }

  batUpdate(now);
  batEvents(now);
  demoService(now);

  // orologio dal sistema, aggiornato una volta al secondo
  static uint32_t lastClock = 0;
  if (clockValid && now - lastClock >= 1000) {
    lastClock = now;
    time_t tt = time(nullptr);
    struct tm tmv;
    gmtime_r(&tt, &tmv);
    snprintf(clockText, sizeof(clockText), "%02d:%02d", tmv.tm_hour, tmv.tm_min);
    // mezzanotte: una volta al giorno, se Clawd e' sveglio
    static int lastMidnightYday = -1;
    if (tmv.tm_hour == 0 && tmv.tm_min == 0 && tmv.tm_yday != lastMidnightYday) {
      lastMidnightYday = tmv.tm_yday;
      if (state == S_IDLE || state == S_DONE) {
        midnightUntil = now + 7000;
        sfxPlay(SFX_HEART);
      }
    }
  }
  if (page == PG_MSG) msgUnread = false;

  // timer pomodoro
  if (tmRunning) {
    tmLeftMs -= (int32_t)(now - tmLast);
    tmLast = now;
    if (tmLeftMs <= 0) {
      tmFocus = !tmFocus;
      tmLeftMs = tmFocus ? TM_FOCUS_MS : TM_BREAK_MS;
      tmRunning = false;
      page = PG_TIMER;
      lastTouch = now;
      sfxPlay(SFX_TIMER);
      if (state == S_SLEEP) setState(S_IDLE, "");
    }
  }

  // dopo un po' senza tocchi si torna a Clawd (tranne il timer in corso)
  if (page != PG_CLAWD && !(page == PG_TIMER && tmRunning) && ago(now, lastTouch) > 45000) page = PG_CLAWD;

  // timeout automatici
  if (state == S_DONE && ago(now, stateSince) > DONE_TO_IDLE_MS) setState(S_IDLE, "");
  if (state == S_ERROR && ago(now, stateSince) > 3000) setState(S_THINKING, "");
  if ((state == S_WORKING || state == S_THINKING) && ago(now, lastEvent) > WORKING_STALE_MS) setState(S_IDLE, "");
  // di notte va a dormire presto; di giorno prima si annoia (vedi drawClawdPage)
  uint32_t sleepAfter = isNight() ? 20000UL : 150000UL;
  if (state == S_IDLE && ago(now, lastEvent) > sleepAfter) state = S_SLEEP;  // senza azzerare lastEvent

  // a batteria: dopo un po' a riposo e senza tocchi, standby automatico
  if (autoStandbyMin && !usbPresent() && batPercent() >= 0 && !tmRunning && (state == S_IDLE || state == S_SLEEP) &&
      ago(now, lastTouch) > autoStandbyMin * 60000UL && ago(now, lastEvent) > autoStandbyMin * 60000UL)
    enterStandby();

  // luminosita': bassa in sleep (protegge l'AMOLED)
  // (un tocco recente la alza per qualche secondo)
  bool dim = state == S_SLEEP && (lastTouch == 0 || ago(now, lastTouch) > 20000);
  setBrightnessSmooth(dim ? 6 : userBrightness);

  if (now - lastFrame >= 33) {  // ~30 fps
    lastFrame = now;
    drawFrame();
  }
}
