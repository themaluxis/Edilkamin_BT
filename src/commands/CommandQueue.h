// Fixed-capacity priority queue with explicit de-duplication and overflow
// accounting. Priority: User > Verify > Poll, FIFO inside a priority.
#pragma once
#include "Command.h"
#include <cstddef>

namespace edk {

class CommandQueue {
public:
  static constexpr size_t kCapacity = 16;

  enum class EnqueueResult : uint8_t {
    Added = 0,
    Replaced,  // superseded a pending write to the same register
    Duplicate, // an identical read was already pending; dropped
    Full,      // no room (User commands evict the oldest poll first)
  };

  using Visitor = void (*)(const Command &, void *ctx);

  // Assigns an id when cmd.id == 0, sets state to Queued.
  // When Replaced, *superseded receives a copy of the command that was dropped.
  EnqueueResult enqueue(Command cmd, Command *superseded = nullptr);

  bool popNext(Command &out);
  bool hasPending() const { return count_ > 0; }
  bool hasPending(CommandSource src) const;
  bool containsRead(uint16_t reg) const;
  size_t size() const { return count_; }
  size_t capacity() const { return kCapacity; }
  uint32_t overflowCount() const { return overflows_; }
  uint32_t highWater() const { return highWater_; }
  uint16_t nextId();

  size_t removeBySource(CommandSource src, Visitor cb = nullptr,
                        void *ctx = nullptr);
  size_t expire(uint32_t now, uint32_t maxAgeMs, Visitor cb = nullptr,
                void *ctx = nullptr);
  void clear();

private:
  static uint8_t priorityOf(CommandSource s);
  int findSlot(bool (*pred)(const Command &, const void *), const void *arg) const;
  int freeSlot() const;
  void removeAt(size_t i);

  Command slots_[kCapacity];
  bool used_[kCapacity] = {false};
  uint32_t seq_[kCapacity] = {0};
  uint32_t seqCounter_ = 0;
  uint16_t idCounter_ = 0;
  size_t count_ = 0;
  uint32_t overflows_ = 0;
  uint32_t highWater_ = 0;
};

} // namespace edk
