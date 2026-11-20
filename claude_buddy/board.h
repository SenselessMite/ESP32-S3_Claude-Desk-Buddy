// SPDX-License-Identifier: MIT
#pragma once
// Variante della scheda Waveshare ESP32-S3-Touch-AMOLED-1.75:
//   1 = versione "C" (32 MB di flash), 0 = versione 1.75 / 1.75-B.
// Cambiano i pin di reset del display e del touch e il clock master dell'audio.
#ifndef BOARD_175C
#define BOARD_175C 1
#endif

#if BOARD_175C
#define LCD_RESET_PIN 1
#define TP_RESET_PIN 2
#define I2S_MCK_PIN 16
#else
#define LCD_RESET_PIN 39
#define TP_RESET_PIN 40
#define I2S_MCK_PIN 42
#endif
