// MQTT topic layout: <prefix>/<deviceId>/<entity>/(state|set)
#pragma once
#include <cstddef>

namespace edk {

class Topics {
public:
  Topics() = default;
  Topics(const char *prefix, const char *deviceId);
  void configure(const char *prefix, const char *deviceId);

  // Build "<prefix>/<deviceId>/<suffix>" into buf; returns buf.
  const char *make(char *buf, size_t n, const char *suffix) const;
  // Convenience: "<entity>/state" and "<entity>/set"
  const char *state(char *buf, size_t n, const char *entity) const;
  const char *set(char *buf, size_t n, const char *entity) const;
  // Given a full topic, extract the entity for "<prefix>/<id>/<entity>/set".
  bool parseSet(const char *topic, char *entity, size_t n) const;

  const char *prefix() const { return prefix_; }
  const char *deviceId() const { return deviceId_; }

private:
  char prefix_[16] = "edilkamin";
  char deviceId_[16] = "322707E4";
};

} // namespace edk
