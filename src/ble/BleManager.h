// NimBLE glue around BleSessionPolicy: scanning (optionally pinned to a MAC),
// connecting, GATT discovery, notifications and encrypted writes.
// NimBLE callbacks run on the BLE host task: they only drop data into
// mailboxes that loop() drains on the Arduino task.
#pragma once
#include "ble/BleSessionPolicy.h"
#include "config/AppConfig.h"
#include "stove/StoveController.h"
#include <esp_arduino_version.h>
#if ESP_ARDUINO_VERSION_MAJOR != 2
#error "BleManager requires Arduino-ESP32 2.x with NimBLE 1.4.3; use the pinned platformio.ini platform."
#endif
#include <NimBLEDevice.h>
#include <atomic>

namespace edk {

class BleManager : public IStoveLink {
public:
  struct Callbacks {
    void (*onFrame)(const uint8_t plain[kBleBlockSize], void *ctx) = nullptr;
    void (*onStateChange)(BleState state, void *ctx) = nullptr;
    void *ctx = nullptr;
  };

  void begin(const AppConfig &cfg, const Callbacks &cb);
  void loop(uint32_t now);

  void setEnabled(bool enabled, uint32_t now) {
    policy_.setEnabled(enabled, now);
  }
  void requestSession(uint32_t now) { policy_.requestSession(now); }
  void setUserPending(bool p) { policy_.setUserPending(p); }
  void setBusy(bool b) { policy_.setBusy(b); }

  // IStoveLink
  bool sendPdu(const uint8_t pdu[kPduSize]) override;

  BleSessionPolicy &policy() { return policy_; }
  const BleSessionPolicy &policy() const { return policy_; }
  bool linkUp() const { return policy_.linkUp(); }
  const char *foundMac() const { return foundMac_; }
  uint32_t framesDropped() const { return framesDropped_; }
  int lastConnectError() const { return lastConnectError_; }

private:
  class AdvCallbacks : public NimBLEAdvertisedDeviceCallbacks {
  public:
    explicit AdvCallbacks(BleManager &m) : m_(m) {}
    void onResult(NimBLEAdvertisedDevice *dev) override;

  private:
    BleManager &m_;
  };
  class ClientCallbacks : public NimBLEClientCallbacks {
  public:
    explicit ClientCallbacks(BleManager &m) : m_(m) {}
    void onConnect(NimBLEClient *) override {}
    void onDisconnect(NimBLEClient *) override;

  private:
    BleManager &m_;
  };

  static void scanCompleteCb(NimBLEScanResults results);
  static void notifyCb(NimBLERemoteCharacteristic *chr, uint8_t *data,
                       size_t len, bool isNotify);
  static BleManager *instance_;

  void startScan();
  void stopScan();
  void connect();
  void disconnect();
  bool matchesTarget(NimBLEAdvertisedDevice *dev);

  BleSessionPolicy policy_;
  Callbacks cb_;
  char targetMac_[18] = "";
  char foundMac_[18] = "";
  uint8_t scanSeconds_ = 10;
  uint8_t connectSeconds_ = 30;
  int lastConnectError_ = 0;

  NimBLEUUID serviceUuid_{"abf0"};
  NimBLEUUID writeUuid_{"abf1"};
  NimBLEUUID readUuid_{"abf2"};
  NimBLEClient *client_ = nullptr;
  NimBLERemoteCharacteristic *writeChr_ = nullptr;
  NimBLERemoteCharacteristic *readChr_ = nullptr;
  AdvCallbacks advCb_{*this};
  ClientCallbacks clientCb_{*this};

  // mailboxes written from the NimBLE task
  std::atomic<bool> foundFlag_{false};
  std::atomic<bool> scanDoneFlag_{false};
  volatile bool disconnectFlag_ = false;
  volatile bool frameFlag_ = false;
  NimBLEAddress foundAddr_;
  int foundRssi_ = 0;
  uint8_t frameBuf_[kBleBlockSize];
  uint32_t framesDropped_ = 0;
  bool scanning_ = false;
  BleState lastReported_ = BleState::Disabled;
};

} // namespace edk
