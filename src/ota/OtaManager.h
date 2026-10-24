// OTA updates with two application slots and automatic rollback (F06).
// Platform layer: wraps the Arduino Update library and the ESP-IDF OTA state
// machine; image checks come from ota/OtaImage (pure).
//
// Lifecycle of an update
//  1. start()   Update.begin on the next OTA slot
//  2. write()   header check on the first 64 bytes, marker scan, flash write
//  3. finish()  marker must have been seen, Update.end verifies the image
//               (checksum + SHA-256) and sets the new slot as boot partition
//  4. reboot    the bootloader boots the new image in PENDING_VERIFY state
//  5. tick()    the new firmware marks itself valid once MQTT connected or
//               after kValidateAfterMs; an intentional restart also validates.
//     Any crash/reset before that makes the bootloader fall back to the
//     previous slot.
#pragma once
#include "ota/OtaImage.h"
#include <cstddef>
#include <cstdint>
#include <esp_ota_ops.h>

namespace edk {

class OtaManager {
public:
  static constexpr uint32_t kValidateAfterMs = 10UL * 60UL * 1000UL;

  struct Info {
    const char *running = "?";
    uint32_t runningSize = 0;
    const char *next = "";          // empty when no second slot exists
    uint32_t nextSize = 0;
    const char *boot = "?";
    const char *runningState = "";
    const char *otherState = "";
    bool pendingVerify = false;
    bool canRollBack = false;
    uint32_t sketchSize = 0;
    uint32_t freeSketch = 0;
  };

  void begin();
  // Returns true the moment the running image gets validated.
  bool tick(uint32_t now, bool healthy);
  void markValid(const char *why);
  bool pendingVerify() const { return pendingVerify_; }
  bool otaCapable() const;
  const char *runningLabel() const;
  Info info() const;

  // upload session (one at a time)
  bool busy() const { return active_; }
  bool start(char *err, size_t errLen);
  bool write(const uint8_t *data, size_t n, char *err, size_t errLen);
  bool finish(char *err, size_t errLen);
  void abort();
  size_t written() const { return written_; }
  const char *imageVersion() const { return scanner_.version(); }

  bool rollBack(char *err, size_t errLen);

  static const char *stateName(esp_ota_img_states_t s);

private:
  bool flushHead(char *err, size_t errLen);
  bool pendingVerify_ = false;
  bool active_ = false;
  bool headChecked_ = false;
  size_t headLen_ = 0;
  uint8_t head_[kOtaHeaderBytes];
  size_t written_ = 0;
  char needle_[32] = "";
  OtaMarkerScanner scanner_;
};

} // namespace edk
