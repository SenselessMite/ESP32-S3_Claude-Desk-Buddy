// SPDX-License-Identifier: MIT
#include "ble.h"
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include "host/ble_store.h"

// Nordic UART Service
#define NUS_SERVICE "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define NUS_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"  // il PC scrive qui
#define NUS_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"  // notifiche verso il PC (non usate per ora)

static const size_t RING = 4096;
static uint8_t ring[RING];
static volatile size_t rHead = 0, rTail = 0;
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool connected = false;
static volatile uint32_t passkey = 0;
static volatile uint32_t passkeyAt = 0;  // 0 = nessun codice da mostrare

static volatile uint32_t pairUntil = 0;  // 0 = finestra chiusa

void bleSetPairing(bool on) {
  pairUntil = on ? (millis() + 120000UL) : 0;
  // con la finestra aperta si mostra il codice sullo schermo (MITM); chiusa, solo "Just Works" che
  // non basta per scrivere sulla caratteristica protetta
  BLESecurity::setCapability(on ? ESP_IO_CAP_OUT : ESP_IO_CAP_NONE);
  if (!on) passkeyAt = 0;
}

bool blePairingOpen(uint32_t *leftMs) {
  uint32_t u = pairUntil;
  if (!u) return false;
  int32_t left = (int32_t)(u - millis());
  if (left <= 0) return false;
  if (leftMs) *leftMs = (uint32_t)left;
  return true;
}

void bleService() {
  if (pairUntil && (int32_t)(pairUntil - millis()) <= 0) bleSetPairing(false);
}

bool blePasskeyActive(uint32_t *code) {
  uint32_t at = passkeyAt;
  if (!at || millis() - at > 60000UL) return false;
  if (code) *code = passkey;
  return true;
}

void bleUnpairAll() { ble_store_clear(); }

// mostra il codice sullo schermo; la scheda non ha tastiera, quindi e' il PC a digitarlo
class SecCb : public BLESecurityCallbacks {
  void onPassKeyNotify(uint32_t k) override {
    passkey = k;
    passkeyAt = millis() ? millis() : 1;
  }
  bool onSecurityRequest() override { return true; }
  void onAuthenticationComplete(ble_gap_conn_desc *) override {
    passkeyAt = 0;
    pairUntil = 0;
    BLESecurity::setCapability(ESP_IO_CAP_NONE);
  }
};

static void ringPush(const uint8_t *d, size_t n) {
  portENTER_CRITICAL(&mux);
  for (size_t i = 0; i < n; i++) {
    size_t nx = (rHead + 1) % RING;
    if (nx == rTail) break;  // pieno: si scarta il resto
    ring[rHead] = d[i];
    rHead = nx;
  }
  portEXIT_CRITICAL(&mux);
}

int bleRead() {
  int v = -1;
  portENTER_CRITICAL(&mux);
  if (rTail != rHead) {
    v = ring[rTail];
    rTail = (rTail + 1) % RING;
  }
  portEXIT_CRITICAL(&mux);
  return v;
}

bool bleConnected() { return connected; }

class ServerCb : public BLEServerCallbacks {
  void onConnect(BLEServer *, ble_gap_conn_desc *desc) override {
    connected = true;
    if (blePairingOpen(nullptr)) BLESecurity::startSecurity(desc->conn_handle);  // avvia l'associazione solo a finestra aperta
  }
  void onDisconnect(BLEServer *, ble_gap_conn_desc *) override {
    connected = false;
    passkeyAt = 0;
    BLEDevice::startAdvertising();
  }
};

class RxCb : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *c) override {
    String v = c->getValue();
    ringPush((const uint8_t *)v.c_str(), v.length());
  }
};

void bleBegin() {
  BLEDevice::init("Claude Buddy");
  BLEDevice::setMTU(247);
  // associazione con codice mostrato sullo schermo (MITM), connessione sicura LE e bonding in flash
  BLEDevice::setSecurityCallbacks(new SecCb());
  BLESecurity::setCapability(ESP_IO_CAP_NONE);  // associazione chiusa finche' non la apri dal menu
  BLESecurity::setAuthenticationMode(true, true, true);
  BLESecurity::setPassKey(false);  // codice casuale a ogni associazione
  BLEServer *srv = BLEDevice::createServer();
  srv->setCallbacks(new ServerCb());
  BLEService *svc = srv->createService(NUS_SERVICE);
  BLECharacteristic *rx = svc->createCharacteristic(NUS_RX, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR | BLECharacteristic::PROPERTY_WRITE_AUTHEN);
  rx->setCallbacks(new RxCb());
  svc->createCharacteristic(NUS_TX, BLECharacteristic::PROPERTY_NOTIFY);
  svc->start();
  BLEAdvertising *adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(NUS_SERVICE);
  adv->setScanResponse(true);
  BLEDevice::startAdvertising();
}
