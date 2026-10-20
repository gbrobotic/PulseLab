// ble_link.h
// Bluetooth Low Energy for PulseLab (ESP32 only, uses the BLE library that
// comes with the ESP32 Arduino core; on the C3 it runs on the NimBLE stack).
//
// Two services:
//   Heart Rate (0x180D)  the standard one: any fitness / BLE app shows the BPM
//   PulseLab             vitals, pulse wave, guided session, and a control
//                        characteristic so the web page can start a session
// The byte layouts are in ble_packets.h.
//
// Only one phone / browser at a time. When it disconnects, advertising starts
// again so it can reconnect.

#pragma once
#include <BLEDevice.h>
#include <BLEServer.h>
#if defined(CONFIG_BLUEDROID_ENABLED)
#include <BLE2902.h>
#endif
#include "ble_packets.h"

namespace pulselab {

// The descriptor that lets a phone switch notifications on (0x2902). NimBLE
// (used on the C3) adds it by itself; the older Bluedroid stack needs it added.
inline void addNotifyDescriptor(BLECharacteristic* c) {
#if defined(CONFIG_BLUEDROID_ENABLED)
  c->addDescriptor(new BLE2902());
#else
  (void)c;
#endif
}

class BleLink : public BLEServerCallbacks, public BLECharacteristicCallbacks {
public:
  void begin(const char* name) {
    BLEDevice::init(name);
    server_ = BLEDevice::createServer();
    server_->setCallbacks(this);

    // Standard Heart Rate service
    BLEService* hrs = server_->createService(BLEUUID((uint16_t)0x180D));
    hrChar_ = hrs->createCharacteristic(BLEUUID((uint16_t)0x2A37), BLECharacteristic::PROPERTY_NOTIFY);
    addNotifyDescriptor(hrChar_);
    BLECharacteristic* loc = hrs->createCharacteristic(BLEUUID((uint16_t)0x2A38), BLECharacteristic::PROPERTY_READ);
    uint8_t finger = 3;   // body sensor location: 3 = finger
    loc->setValue(&finger, 1);
    hrs->start();

    // PulseLab service
    BLEService* pl = server_->createService(BLEUUID(BLE_PULSELAB_SERVICE_UUID), 30);
    vitals_ = notifyChar(pl, BLE_VITALS_UUID);
    wave_   = notifyChar(pl, BLE_WAVE_UUID);
    guided_ = notifyChar(pl, BLE_GUIDED_UUID);
    result_ = pl->createCharacteristic(BLE_RESULT_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
    addNotifyDescriptor(result_);
    uint8_t empty[RESULT_LEN] = {0};
    result_->setValue(empty, RESULT_LEN);
    control_ = pl->createCharacteristic(BLE_CONTROL_UUID, BLECharacteristic::PROPERTY_WRITE);
    control_->setCallbacks(this);
    pl->start();

    BLEAdvertising* adv = BLEDevice::getAdvertising();
    adv->addServiceUUID(BLEUUID((uint16_t)0x180D));     // so heart rate apps find it
    adv->setScanResponse(true);                          // name + PulseLab UUID go in the scan response
    adv->setMinPreferred(0x06);
    BLEDevice::startAdvertising();
  }

  bool connected() const { return connected_; }
  uint32_t takeNotifyCount() { uint32_t n = notifies_; notifies_ = 0; return n; }

  // Call from loop(): restarts advertising half a second after a disconnect.
  void loop(uint32_t now) {
    if (restartAt_ && (int32_t)(now - restartAt_) >= 0) {
      restartAt_ = 0;
      BLEDevice::startAdvertising();
    }
  }

  // A command written by the web page (CMD_...), or 0. Read once.
  uint8_t takeCommand() { uint8_t c = command_; command_ = 0; return c; }

  void sendHeartRate(float bpm, bool finger, uint16_t rrMs) {
    uint8_t p[4];
    send(hrChar_, p, packHeartRate(p, bpm, finger, rrMs));
  }
  void sendVitals(const uint8_t* p) { send(vitals_, p, VITALS_LEN); }
  void sendWave(const uint8_t* p)   { send(wave_, p, BLE_WAVE_LEN); }
  void sendGuided(const uint8_t* p) { send(guided_, p, GUIDED_LEN); }
  void sendResult(const uint8_t* p) {
    result_->setValue((uint8_t*)p, RESULT_LEN);       // also readable later
    if (connected_) { result_->notify(); notifies_++; }
  }

  // BLEServerCallbacks
  void onConnect(BLEServer*) override { connected_ = true; }
  void onDisconnect(BLEServer*) override { connected_ = false; restartAt_ = millis() + 500; if (!restartAt_) restartAt_ = 1; }

  // BLECharacteristicCallbacks (control)
  void onWrite(BLECharacteristic* c) override {
    if (c->getLength() >= 1) command_ = c->getData()[0];
  }

private:
  BLECharacteristic* notifyChar(BLEService* s, const char* uuid) {
    BLECharacteristic* c = s->createCharacteristic(uuid, BLECharacteristic::PROPERTY_NOTIFY);
    addNotifyDescriptor(c);
    return c;
  }
  void send(BLECharacteristic* c, const uint8_t* p, size_t n) {
    if (!connected_) return;           // nobody listening: skip the work
    c->setValue((uint8_t*)p, n);
    c->notify();
    notifies_++;
  }

  BLEServer* server_ = nullptr;
  BLECharacteristic *hrChar_ = nullptr, *vitals_ = nullptr, *wave_ = nullptr, *guided_ = nullptr, *result_ = nullptr, *control_ = nullptr;
  volatile bool    connected_ = false;
  volatile uint8_t command_ = 0;
  volatile uint32_t restartAt_ = 0;
  uint32_t notifies_ = 0;
};

}  // namespace pulselab
