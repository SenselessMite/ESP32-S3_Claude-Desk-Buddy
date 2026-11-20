// SPDX-License-Identifier: MIT
#pragma once
// Trasporto BLE (Nordic UART Service): il PC scrive le stesse righe di testo che arrivano
// dalla seriale USB. I byte ricevuti finiscono in un buffer circolare, letti dal loop().
#include <stdint.h>

void bleBegin();
bool bleConnected();
int bleRead();  // prossimo byte ricevuto, -1 se non ce ne sono

// Associazione (pairing) sicura: la scheda mostra a schermo un codice a 6 cifre da digitare sul PC.
bool blePasskeyActive(uint32_t *code);  // true mentre il codice va mostrato
void bleUnpairAll();                    // dimentica tutti i PC associati

// Finestra di associazione: fuori da essa la scheda accetta solo PC gia' associati.
void bleSetPairing(bool on);                // apre (120 s) o chiude la finestra
bool blePairingOpen(uint32_t *leftMs);      // true mentre la finestra e' aperta
void bleService();                          // da chiamare nel loop: chiude la finestra scaduta
