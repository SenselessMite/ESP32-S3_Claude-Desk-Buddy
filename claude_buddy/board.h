// SPDX-License-Identifier: MIT
#pragma once
// Variante della scheda Waveshare ESP32-S3-Touch-AMOLED-1.75:
//   1 = versione "C" (32 MB di flash), 0 = versione 1.75 / 1.75-B.
// Cambiano i pin di reset del display e del touch e il clock master dell'audio.
#ifndef BOARD_175C
#define BOARD_175C 1
#endif

// Orientamento automatico: come e' montato l'accelerometro rispetto allo schermo.
// Per ogni bordo dello schermo (alto, destro, basso, sinistro): asse del sensore (0 = X, 1 = Y)
// e segno con cui l'accelerometro lo legge quando quel bordo guarda IN SU (a riposo misura +1 g
// verso l'alto). Per tararlo: comando seriale `orientdebug 1`, stampa gx/gy e la rotazione scelta.
// Se la schermata ruota nel verso sbagliato, scambia i segni dei bordi destro e sinistro
// (o dell'alto e del basso); se ruota di 90 gradi di troppo, cambia gli assi.
#define IMU_EDGE_AXIS { 1, 0, 1, 0 }
#define IMU_EDGE_SIGN { 1, 1, -1, -1 }
// Sfasamento fisso (in passi di 90 gradi, mod 4) sommato alla rotazione scelta: tarato sulla scheda
// in prova, dove l'immagine risultava ruotata di 90 gradi in senso orario. Se invece risulta ruotata
// in senso antiorario metti 1; se di 180 gradi metti 2; con 0 nessuno sfasamento.
#define IMU_ROT_SHIFT 3

#if BOARD_175C
#define LCD_RESET_PIN 1
#define TP_RESET_PIN 2
#define I2S_MCK_PIN 16
#else
#define LCD_RESET_PIN 39
#define TP_RESET_PIN 40
#define I2S_MCK_PIN 42
#endif
