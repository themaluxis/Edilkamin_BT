#include "BleManager.h"
#include "core/Log.h"
#include "core/Strings.h"
#include "logging/Logger.h"
#include "protocol/Crypto.h"
#include <NimBLEUtils.h>
#include <cstring>
#include <ctime>

namespace edk {

static const char *kTag = "BLE";
BleManager *BleManager::instance_ = nullptr;

void BleManager::begin(const AppConfig &cfg, const Callbacks &cb) {
  instance_ = this;
  cb_ = cb;
  copyStr(targetMac_, sizeof(targetMac_), cfg.stoveMac);
  scanSeconds_ = (uint8_t)cfg.bleScanTimeoutS;
  // Preserve the original firmware's NimBLE default connection timeout.
  connectSeconds_ = 30;

  BlePolicyConfig pc;
  pc.sessionMs = (uint32_t)cfg.bleSessionS * 1000UL;
  pc.pauseMs = (uint32_t)cfg.blePauseS * 1000UL;
  pc.reconnectInitialMs = (uint32_t)cfg.bleReconnectInitialS * 1000UL;
  pc.reconnectMaxMs = (uint32_t)cfg.bleReconnectMaxS * 1000UL;
  pc.scanTimeoutMs = (uint32_t)cfg.bleScanTimeoutS * 1000UL + 2000UL;
  policy_.configure(pc);

  NimBLEDevice::init("");
  NimBLEScan *scan = NimBLEDevice::getScan();
  scan->setAdvertisedDeviceCallbacks(&advCb_, false);
  scan->setInterval(1349);
  scan->setWindow(449);
  scan->setActiveScan(true);
  client_ = NimBLEDevice::createClient();
  client_->setClientCallbacks(&clientCb_, false);
  client_->setConnectTimeout(connectSeconds_);
  EDK_LOGI(kTag, "init done, target %s", targetMac_[0] ? targetMac_ : "(any stove)");
}

// ---- NimBLE task context ---------------------------------------------------

bool BleManager::matchesTarget(NimBLEAdvertisedDevice *dev) {
  if (!dev->haveServiceUUID() || !dev->isAdvertisingService(serviceUuid_))
    return false;
  if (targetMac_[0] == '\0')
    return true;
  std::string addr = dev->getAddress().toString();
  if (addr.size() != 17)
    return false;
  for (size_t i = 0; i < 17; i++) {
    char a = addr[i], b = targetMac_[i];
    if (a >= 'a' && a <= 'f')
      a = (char)(a - 'a' + 'A');
    if (a != b)
      return false;
  }
  return true;
}

void BleManager::AdvCallbacks::onResult(NimBLEAdvertisedDevice *dev) {
  if (m_.foundFlag_ || !m_.matchesTarget(dev))
    return;
  m_.foundRssi_ = dev->getRSSI();
  m_.foundAddr_ = dev->getAddress();
  m_.foundFlag_ = true;
  NimBLEDevice::getScan()->stop();
}

void BleManager::scanCompleteCb(NimBLEScanResults) {
  if (instance_)
    instance_->scanDoneFlag_ = true;
}

void BleManager::ClientCallbacks::onDisconnect(NimBLEClient *) {
  m_.disconnectFlag_ = true;
}

void BleManager::notifyCb(NimBLERemoteCharacteristic *, uint8_t *data,
                          size_t len, bool) {
  if (!instance_)
    return;
  if (len != kBleBlockSize) {
    instance_->framesDropped_++;
    return;
  }
  if (instance_->frameFlag_)
    instance_->framesDropped_++; // previous frame not consumed yet
  memcpy(instance_->frameBuf_, data, kBleBlockSize);
  instance_->frameFlag_ = true;
}

// ---- Arduino task context ----------------------------------------------------

void BleManager::startScan() {
  NimBLEScan *scan = NimBLEDevice::getScan();
  if (scan->isScanning())
    scan->stop();
  scan->clearResults();
  // stop() can invoke scanCompleteCb: discard that event before a new scan.
  foundFlag_ = false;
  scanDoneFlag_ = false;
  scanning_ = scan->start(scanSeconds_, scanCompleteCb, false);
  if (!scanning_)
    EDK_LOGW(kTag, "scan start failed");
  else
    EDK_LOGD(kTag, "scanning for %s (%us)", targetMac_[0] ? targetMac_ : "service abf0",
             scanSeconds_);
}

void BleManager::stopScan() {
  NimBLEScan *scan = NimBLEDevice::getScan();
  if (scan->isScanning())
    scan->stop();
  scanning_ = false;
}

void BleManager::connect() {
  std::string addr = foundAddr_.toString();
  copyStr(foundMac_, sizeof(foundMac_), addr.c_str());
  for (char *p = foundMac_; *p; p++)
    if (*p >= 'a' && *p <= 'f')
      *p = (char)(*p - 'a' + 'A');
  EDK_LOGI(kTag, "connecting to %s (RSSI %d dBm, addr type %u)",
           foundMac_, foundRssi_, (unsigned)foundAddr_.getType());
  policy_.stats_connectAttempt();
  disconnectFlag_ = false;
  writeChr_ = nullptr;
  readChr_ = nullptr;
  lastConnectError_ = 0;
  if (!client_->connect(foundAddr_)) {
    const int rc = client_->getLastError();
    lastConnectError_ = rc;
    EDK_LOGW(kTag, "connect failed: rc=%d (0x%X), %s", rc, (unsigned)rc,
             rc ? NimBLEUtils::returnCodeToString(rc)
                : "no stack error recorded; see NimBLE serial log");
    policy_.onConnectResult(false, BleErrorReason::ConnectFailed, millis());
    return;
  }
  NimBLERemoteService *svc = client_->getService(serviceUuid_);
  if (!svc) {
    EDK_LOGW(kTag, "service abf0 not found");
    client_->disconnect();
    policy_.onConnectResult(false, BleErrorReason::ServiceMissing, millis());
    return;
  }
  writeChr_ = svc->getCharacteristic(writeUuid_);
  readChr_ = svc->getCharacteristic(readUuid_);
  if (!writeChr_ || !readChr_ || !readChr_->canNotify()) {
    EDK_LOGW(kTag, "characteristics abf1/abf2 not usable");
    client_->disconnect();
    policy_.onConnectResult(false, BleErrorReason::ServiceMissing, millis());
    return;
  }
  frameFlag_ = false;
  if (!readChr_->subscribe(true, notifyCb, false)) {
    EDK_LOGW(kTag, "subscribe to abf2 failed");
    client_->disconnect();
    policy_.onConnectResult(false, BleErrorReason::ServiceMissing, millis());
    return;
  }
  EDK_LOGI(kTag, "GATT ready (MTU %u)", client_->getMTU());
  policy_.onConnectResult(true, BleErrorReason::None, millis());
}

void BleManager::disconnect() {
  if (client_ && client_->isConnected())
    client_->disconnect();
  writeChr_ = nullptr;
  readChr_ = nullptr;
}

bool BleManager::sendPdu(const uint8_t pdu[kPduSize]) {
  if (!client_ || !client_->isConnected() || !writeChr_)
    return false;
  uint8_t plain[kBleBlockSize];
  uint8_t enc[kBleBlockSize];
  buildPlainBlock((uint32_t)time(nullptr), pdu, plain);
  aesEncryptBlock(plain, enc);
  if (logger().enabled(LogLevel::Trace)) {
    char hex[2 * kPduSize + 1];
    for (size_t i = 0; i < kPduSize; i++)
      snprintf(hex + 2 * i, 3, "%02X", pdu[i]);
    EDK_LOGT(kTag, "tx pdu %s", hex);
  }
  return writeChr_->writeValue(enc, kBleBlockSize, false);
}

void BleManager::loop(uint32_t now) {
  // 1. events from the NimBLE task
  if (disconnectFlag_) {
    disconnectFlag_ = false;
    writeChr_ = nullptr;
    readChr_ = nullptr;
    EDK_LOGI(kTag, "link closed");
    policy_.onLinkLost(now);
  }
  // onResult publishes the candidate before calling stop(). Wait for stop's
  // completion callback before connecting, including across the ESP32 cores.
  if (scanDoneFlag_.exchange(false)) {
    scanning_ = false;
    if (foundFlag_.exchange(false)) {
      policy_.onScanFound(now);
      if (policy_.state() == BleState::Connecting)
        connect();
    } else {
      policy_.onScanFinished(now); // ignored unless still scanning
    }
  }
  if (frameFlag_) {
    uint8_t enc[kBleBlockSize];
    memcpy(enc, frameBuf_, kBleBlockSize);
    frameFlag_ = false;
    uint8_t plain[kBleBlockSize];
    aesDecryptBlock(enc, plain);
    if (logger().enabled(LogLevel::Trace)) {
      char hex[2 * 12 + 1];
      for (size_t i = 0; i < 12; i++)
        snprintf(hex + 2 * i, 3, "%02X", plain[kFrameHeaderSize + i]);
      EDK_LOGT(kTag, "rx body %s", hex);
    }
    if (cb_.onFrame)
      cb_.onFrame(plain, cb_.ctx);
  }

  // 2. policy decision
  // Connection/GATT operations and frame callbacks can advance the clock.
  now = millis();
  switch (policy_.tick(now)) {
  case BleAction::StartScan:
    startScan();
    break;
  case BleAction::StopScan:
    stopScan();
    break;
  case BleAction::Disconnect:
    disconnect();
    break;
  case BleAction::Connect:
  case BleAction::None:
    break;
  }

  // 3. report state changes
  if (policy_.state() != lastReported_) {
    lastReported_ = policy_.state();
    if (cb_.onStateChange)
      cb_.onStateChange(lastReported_, cb_.ctx);
  }
}

} // namespace edk
