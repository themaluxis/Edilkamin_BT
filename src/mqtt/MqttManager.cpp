#include "MqttManager.h"
#include "core/Log.h"
#include "core/Strings.h"

namespace edk {

static const char *kTag = "MQTT";
static const uint32_t kBackoffInitialMs = 5000;
static const uint32_t kBackoffMaxMs = 60000;

MqttManager *MqttManager::instance_ = nullptr;

void MqttManager::begin(Client &net, const char *server, uint16_t port,
                        const char *user, const char *pass,
                        const char *clientId, const char *willTopic) {
  instance_ = this;
  copyStr(server_, sizeof(server_), server);
  port_ = port;
  copyStr(user_, sizeof(user_), user);
  copyStr(pass_, sizeof(pass_), pass);
  copyStr(clientId_, sizeof(clientId_), clientId);
  copyStr(willTopic_, sizeof(willTopic_), willTopic);
  client_.setClient(net);
  client_.setServer(server_, port_);
  client_.setBufferSize(2048);
  client_.setKeepAlive(60);
  client_.setSocketTimeout(5);
  client_.setCallback(dispatch);
}

void MqttManager::setHandlers(MessageHandler onMessage,
                              ConnectHandler onConnect, void *ctx) {
  onMessage_ = onMessage;
  onConnect_ = onConnect;
  ctx_ = ctx;
}

bool MqttManager::addSubscription(const char *topic, bool clearRetained) {
  if (subCount_ >= kMaxSubscriptions)
    return false;
  copyStr(subs_[subCount_], kTopicLen, topic);
  subClear_[subCount_] = clearRetained;
  subCount_++;
  return true;
}

void MqttManager::dispatch(char *topic, uint8_t *payload,
                           unsigned int length) {
  if (!instance_ || !instance_->onMessage_)
    return;
  // Copy both out of PubSubClient's buffer: a publish made from inside the
  // handler (results, logs) reuses that buffer.
  char tbuf[kTopicLen];
  copyStr(tbuf, sizeof(tbuf), topic);
  char buf[kMaxPayload];
  size_t n = length < sizeof(buf) - 1 ? length : sizeof(buf) - 1;
  memcpy(buf, payload, n);
  buf[n] = '\0';
  if (length >= sizeof(buf))
    EDK_LOGW(kTag, "payload on %s truncated (%u bytes)", tbuf, length);
  instance_->onMessage_(tbuf, buf, instance_->ctx_);
}

const char *MqttManager::stateName() {
  switch (client_.state()) {
  case MQTT_CONNECTION_TIMEOUT:
    return "connection timeout";
  case MQTT_CONNECTION_LOST:
    return "connection lost";
  case MQTT_CONNECT_FAILED:
    return "connect failed";
  case MQTT_DISCONNECTED:
    return "disconnected";
  case MQTT_CONNECTED:
    return "connected";
  case MQTT_CONNECT_BAD_PROTOCOL:
    return "bad protocol";
  case MQTT_CONNECT_BAD_CLIENT_ID:
    return "bad client id";
  case MQTT_CONNECT_UNAVAILABLE:
    return "server unavailable";
  case MQTT_CONNECT_BAD_CREDENTIALS:
    return "bad credentials";
  case MQTT_CONNECT_UNAUTHORIZED:
    return "unauthorized";
  }
  return "?";
}

bool MqttManager::connectOnce(uint32_t now) {
  EDK_LOGI(kTag, "connecting to %s:%u as %s", server_, port_, clientId_);
  bool ok;
  const char *user = user_[0] ? user_ : nullptr;
  const char *pass = pass_[0] ? pass_ : nullptr;
  ok = client_.connect(clientId_, user, pass, willTopic_, 1, true, "OFFLINE",
                       true);
  if (!ok) {
    EDK_LOGW(kTag, "connect failed: %s (rc=%d)", stateName(), client_.state());
    backoffMs_ = backoffMs_ == 0 ? kBackoffInitialMs
                                 : (backoffMs_ * 2 > kBackoffMaxMs
                                        ? kBackoffMaxMs
                                        : backoffMs_ * 2);
    nextAttemptAt_ = now + backoffMs_;
    return false;
  }
  backoffMs_ = 0;
  if (everConnected_)
    reconnects_++;
  everConnected_ = true;
  afterConnect();
  return true;
}

void MqttManager::afterConnect() {
  EDK_LOGI(kTag, "connected");
  invalidateCache();
  publish(willTopic_, "ONLINE", true, true);
  // Wipe retained commands before subscribing: a retained "heat" from an old
  // Home Assistant configuration must never ignite the stove on boot.
  for (size_t i = 0; i < subCount_; i++) {
    if (subClear_[i])
      client_.publish(subs_[i], (const uint8_t *)nullptr, 0, true);
  }
  for (size_t i = 0; i < subCount_; i++) {
    if (!client_.subscribe(subs_[i], 1))
      EDK_LOGW(kTag, "subscribe failed: %s", subs_[i]);
  }
  if (onConnect_)
    onConnect_(ctx_);
}

void MqttManager::loop(uint32_t now, bool networkUp) {
  if (client_.connected()) {
    wasConnected_ = true;
    client_.loop();
    return;
  }
  if (wasConnected_) {
    wasConnected_ = false;
    EDK_LOGW(kTag, "connection lost: %s", stateName());
    nextAttemptAt_ = now + 1000;
  }
  if (!networkUp)
    return;
  if ((int32_t)(now - nextAttemptAt_) < 0)
    return;
  nextAttemptAt_ = now + (backoffMs_ ? backoffMs_ : kBackoffInitialMs);
  connectOnce(now);
}

bool MqttManager::publish(const char *topic, const char *payload, bool retain,
                          bool force) {
  if (!client_.connected())
    return false;
  uint32_t th = fnv1a(topic);
  uint32_t ph = fnv1a(payload ? payload : "");
  CacheEntry *slot = nullptr;
  for (auto &e : cache_) {
    if (e.used && e.topicHash == th) {
      slot = &e;
      break;
    }
  }
  if (!force && slot && slot->payloadHash == ph && slot->retain == retain)
    return false; // unchanged → do not flood the broker
  bool ok = client_.publish(topic, payload, retain);
  if (!ok) {
    EDK_LOGW(kTag, "publish failed on %s (%u bytes)", topic,
             (unsigned)strlen(payload ? payload : ""));
    return false;
  }
  if (!slot) {
    slot = &cache_[cacheNext_];
    cacheNext_ = (cacheNext_ + 1) % kCacheEntries;
    slot->topicHash = th;
    slot->used = true;
  }
  slot->payloadHash = ph;
  slot->retain = retain;
  return true;
}

void MqttManager::invalidateCache() {
  for (auto &e : cache_)
    e = CacheEntry();
  cacheNext_ = 0;
}

} // namespace edk
