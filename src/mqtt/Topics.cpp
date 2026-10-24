#include "Topics.h"
#include "core/Strings.h"
#include <cstdio>
#include <cstring>

namespace edk {

Topics::Topics(const char *prefix, const char *deviceId) {
  configure(prefix, deviceId);
}

void Topics::configure(const char *prefix, const char *deviceId) {
  if (prefix && *prefix)
    copyStr(prefix_, sizeof(prefix_), prefix);
  if (deviceId && *deviceId)
    copyStr(deviceId_, sizeof(deviceId_), deviceId);
}

const char *Topics::make(char *buf, size_t n, const char *suffix) const {
  snprintf(buf, n, "%s/%s/%s", prefix_, deviceId_, suffix);
  return buf;
}

const char *Topics::state(char *buf, size_t n, const char *entity) const {
  snprintf(buf, n, "%s/%s/%s/state", prefix_, deviceId_, entity);
  return buf;
}

const char *Topics::set(char *buf, size_t n, const char *entity) const {
  snprintf(buf, n, "%s/%s/%s/set", prefix_, deviceId_, entity);
  return buf;
}

bool Topics::parseSet(const char *topic, char *entity, size_t n) const {
  if (!topic || !entity || n == 0)
    return false;
  char base[40];
  snprintf(base, sizeof(base), "%s/%s/", prefix_, deviceId_);
  size_t bl = strlen(base);
  if (strncmp(topic, base, bl) != 0)
    return false;
  const char *rest = topic + bl;
  const char *slash = strchr(rest, '/');
  if (!slash || strcmp(slash, "/set") != 0)
    return false;
  size_t len = (size_t)(slash - rest);
  if (len == 0 || len >= n)
    return false;
  memcpy(entity, rest, len);
  entity[len] = '\0';
  return true;
}

} // namespace edk
