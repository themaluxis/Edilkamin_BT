// PubSubClient wrapper: Last Will, non-blocking reconnect with backoff,
// de-duplicated publishing and retained-command clearing (HA01 / §8).
#pragma once
#include <Client.h>
#include <PubSubClient.h>
#include <cstddef>
#include <cstdint>

namespace edk {

class MqttManager {
public:
  static constexpr size_t kMaxSubscriptions = 24;
  static constexpr size_t kTopicLen = 80;
  static constexpr size_t kMaxPayload = 160;
  static constexpr size_t kCacheEntries = 64;

  using MessageHandler = void (*)(const char *topic, const char *payload,
                                  void *ctx);
  using ConnectHandler = void (*)(void *ctx);

  void begin(Client &net, const char *server, uint16_t port, const char *user,
             const char *pass, const char *clientId, const char *willTopic);
  void setHandlers(MessageHandler onMessage, ConnectHandler onConnect,
                   void *ctx);
  // Subscribed on every (re)connect. When clearRetained is set, a retained
  // empty message is published first so stale retained commands can never be
  // replayed into the stove after a reboot.
  bool addSubscription(const char *topic, bool clearRetained);

  // Call from loop(). `networkUp` gates connection attempts.
  void loop(uint32_t now, bool networkUp);
  bool connected() { return client_.connected(); }

  // Publishes only when the payload differs from the last one sent on that
  // topic (unless force). Returns true when a message went out.
  bool publish(const char *topic, const char *payload, bool retain = false,
               bool force = false);
  void invalidateCache();

  uint32_t reconnectCount() const { return reconnects_; }
  int lastState() { return client_.state(); }
  const char *stateName();

private:
  struct CacheEntry {
    uint32_t topicHash = 0;
    uint32_t payloadHash = 0;
    bool retain = false;
    bool used = false;
  };

  static void dispatch(char *topic, uint8_t *payload, unsigned int length);
  bool connectOnce(uint32_t now);
  void afterConnect();

  static MqttManager *instance_;
  PubSubClient client_;
  MessageHandler onMessage_ = nullptr;
  ConnectHandler onConnect_ = nullptr;
  void *ctx_ = nullptr;
  char server_[64] = "";
  uint16_t port_ = 1883;
  char user_[48] = "";
  char pass_[64] = "";
  char clientId_[40] = "";
  char willTopic_[kTopicLen] = "";
  char subs_[kMaxSubscriptions][kTopicLen];
  bool subClear_[kMaxSubscriptions] = {false};
  size_t subCount_ = 0;
  CacheEntry cache_[kCacheEntries];
  size_t cacheNext_ = 0;
  uint32_t nextAttemptAt_ = 0;
  uint32_t backoffMs_ = 0;
  uint32_t reconnects_ = 0;
  bool wasConnected_ = false;
  bool everConnected_ = false;
};

} // namespace edk
