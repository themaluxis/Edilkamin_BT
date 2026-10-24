#include "OtaManager.h"
#include "core/Log.h"
#include "core/Strings.h"
#include <Arduino.h>
#include <Update.h>
#include <cstdio>
#include <cstring>

#ifndef FW_VERSION
#define FW_VERSION "dev"
#endif

// Marker found by OtaMarkerScanner in uploaded images. Kept as one literal so
// it is contiguous in .rodata; the scanner builds its needle from two halves.
extern "C" const char edk_fw_marker[] __attribute__((used)) =
    "EDILKAMIN_BT_FW_V1:" FW_VERSION;

// Arduino core hook: defer the OTA validation to OtaManager::tick().
extern "C" bool verifyRollbackLater() { return true; }

namespace edk {

static const char *kTag = "OTA";

const char *OtaManager::stateName(esp_ota_img_states_t s) {
  switch (s) {
  case ESP_OTA_IMG_NEW:
    return "new";
  case ESP_OTA_IMG_PENDING_VERIFY:
    return "pending_verify";
  case ESP_OTA_IMG_VALID:
    return "valid";
  case ESP_OTA_IMG_INVALID:
    return "invalid";
  case ESP_OTA_IMG_ABORTED:
    return "aborted";
  case ESP_OTA_IMG_UNDEFINED:
  default:
    return "undefined";
  }
}

static const char *partState(const esp_partition_t *p) {
  esp_ota_img_states_t st;
  if (!p || esp_ota_get_state_partition(p, &st) != ESP_OK)
    return "";
  return OtaManager::stateName(st);
}

void OtaManager::begin() {
  otaMarkerPrefix(needle_, sizeof(needle_));
  const esp_partition_t *run = esp_ota_get_running_partition();
  esp_ota_img_states_t st;
  pendingVerify_ = run && esp_ota_get_state_partition(run, &st) == ESP_OK &&
                   st == ESP_OTA_IMG_PENDING_VERIFY;
  const esp_partition_t *next = esp_ota_get_next_update_partition(nullptr);
  // Logging the marker's version part is also what keeps the marker string
  // referenced, so --gc-sections cannot drop it from the image.
  EDK_LOGI(kTag, "running %s (%s), image %s, %s", run ? run->label : "?",
           partState(run), edk_fw_marker + strlen(needle_),
           next ? "second slot available" : "single slot: OTA disabled");
  if (pendingVerify_)
    EDK_LOGW(kTag, "first boot after update: validation pending (auto-rollback armed)");
}

bool OtaManager::tick(uint32_t now, bool healthy) {
  if (!pendingVerify_)
    return false;
  if (healthy)
    markValid("MQTT connected");
  else if (now >= kValidateAfterMs)
    markValid("10 min uptime");
  return !pendingVerify_;
}

void OtaManager::markValid(const char *why) {
  if (!pendingVerify_)
    return;
  esp_err_t e = esp_ota_mark_app_valid_cancel_rollback();
  pendingVerify_ = false;
  if (e == ESP_OK)
    EDK_LOGI(kTag, "image validated (%s); rollback disarmed", why);
  else
    EDK_LOGE(kTag, "mark valid failed: %d", (int)e);
}

bool OtaManager::otaCapable() const {
  return esp_ota_get_next_update_partition(nullptr) != nullptr;
}

const char *OtaManager::runningLabel() const {
  const esp_partition_t *run = esp_ota_get_running_partition();
  return run ? run->label : "?";
}

OtaManager::Info OtaManager::info() const {
  Info i;
  const esp_partition_t *run = esp_ota_get_running_partition();
  const esp_partition_t *next = esp_ota_get_next_update_partition(nullptr);
  const esp_partition_t *boot = esp_ota_get_boot_partition();
  if (run) {
    i.running = run->label;
    i.runningSize = run->size;
    i.runningState = partState(run);
  }
  if (next) {
    i.next = next->label;
    i.nextSize = next->size;
    i.otherState = partState(next);
  }
  if (boot)
    i.boot = boot->label;
  i.pendingVerify = pendingVerify_;
  i.canRollBack = Update.canRollBack();
  i.sketchSize = ESP.getSketchSize();
  i.freeSketch = ESP.getFreeSketchSpace();
  return i;
}

bool OtaManager::start(char *err, size_t errLen) {
  if (active_) {
    copyStr(err, errLen, "an update is already in progress");
    return false;
  }
  if (!otaCapable()) {
    copyStr(err, errLen, "no second application slot (flash the two-slot partition table over USB first)");
    return false;
  }
  if (pendingVerify_) {
    copyStr(err, errLen, "current image not validated yet; wait for MQTT or 10 minutes");
    return false;
  }
  if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
    snprintf(err, errLen, "cannot start update: %s", Update.errorString());
    return false;
  }
  active_ = true;
  headChecked_ = false;
  headLen_ = 0;
  written_ = 0;
  scanner_.begin(needle_);
  EDK_LOGW(kTag, "update started (slot %s)", esp_ota_get_next_update_partition(nullptr)->label);
  return true;
}

bool OtaManager::flushHead(char *err, size_t errLen) {
  OtaHeaderInfo hi;
  const char *why = otaCheckImageHeader(head_, headLen_, &hi);
  if (why) {
    copyStr(err, errLen, why);
    return false;
  }
  headChecked_ = true;
  scanner_.feed(head_, headLen_);
  if (Update.write(head_, headLen_) != headLen_) {
    snprintf(err, errLen, "flash write failed: %s", Update.errorString());
    return false;
  }
  written_ += headLen_;
  return true;
}

bool OtaManager::write(const uint8_t *data, size_t n, char *err, size_t errLen) {
  if (!active_) {
    copyStr(err, errLen, "no update in progress");
    return false;
  }
  size_t off = 0;
  if (!headChecked_) {
    size_t take = kOtaHeaderBytes - headLen_;
    if (take > n)
      take = n;
    memcpy(head_ + headLen_, data, take);
    headLen_ += take;
    off = take;
    if (headLen_ < kOtaHeaderBytes)
      return true; // wait for more
    if (!flushHead(err, errLen)) {
      abort();
      return false;
    }
  }
  if (off < n) {
    scanner_.feed(data + off, n - off);
    if (Update.write((uint8_t *)data + off, n - off) != n - off) {
      snprintf(err, errLen, "flash write failed: %s", Update.errorString());
      abort();
      return false;
    }
    written_ += n - off;
  }
  return true;
}

bool OtaManager::finish(char *err, size_t errLen) {
  if (!active_) {
    copyStr(err, errLen, "no update in progress");
    return false;
  }
  if (!headChecked_) {
    abort();
    copyStr(err, errLen, "image too short");
    return false;
  }
  if (!scanner_.found()) {
    abort();
    copyStr(err, errLen, "not an Edilkamin_BT Enhanced firmware (marker missing)");
    return false;
  }
  if (!Update.end(true)) {
    snprintf(err, errLen, "image verification failed: %s", Update.errorString());
    active_ = false;
    return false;
  }
  active_ = false;
  EDK_LOGW(kTag, "update written: %u bytes, version %s, md5 %s; restart to boot it",
           (unsigned)written_, scanner_.version(), Update.md5String().c_str());
  return true;
}

void OtaManager::abort() {
  if (active_) {
    Update.abort();
    EDK_LOGW(kTag, "update aborted after %u bytes", (unsigned)written_);
  }
  active_ = false;
}

bool OtaManager::rollBack(char *err, size_t errLen) {
  if (active_) {
    copyStr(err, errLen, "an update is in progress");
    return false;
  }
  if (!Update.canRollBack()) {
    copyStr(err, errLen, "the other slot holds no valid firmware");
    return false;
  }
  if (!Update.rollBack()) {
    snprintf(err, errLen, "rollback failed: %s", Update.errorString());
    return false;
  }
  EDK_LOGW(kTag, "boot partition switched to the previous firmware");
  return true;
}

} // namespace edk
