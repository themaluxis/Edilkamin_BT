#include "CommandQueue.h"

namespace edk {

uint8_t CommandQueue::priorityOf(CommandSource s) {
  switch (s) {
  case CommandSource::User:
    return 2;
  case CommandSource::Verify:
    return 1;
  case CommandSource::Poll:
  default:
    return 0;
  }
}

uint16_t CommandQueue::nextId() {
  if (++idCounter_ == 0)
    idCounter_ = 1;
  return idCounter_;
}

int CommandQueue::freeSlot() const {
  for (size_t i = 0; i < kCapacity; i++)
    if (!used_[i])
      return (int)i;
  return -1;
}

void CommandQueue::removeAt(size_t i) {
  if (!used_[i])
    return;
  used_[i] = false;
  slots_[i] = Command();
  count_--;
}

CommandQueue::EnqueueResult CommandQueue::enqueue(Command cmd,
                                                  Command *superseded) {
  if (cmd.id == 0)
    cmd.id = nextId();
  cmd.state = CommandState::Queued;

  if (cmd.isWrite()) {
    // Latest write to a register wins; keep the slot position of the old one.
    for (size_t i = 0; i < kCapacity; i++) {
      if (used_[i] && slots_[i].isWrite() && slots_[i].reg == cmd.reg) {
        if (superseded)
          *superseded = slots_[i];
        slots_[i] = cmd;
        return EnqueueResult::Replaced;
      }
    }
  } else {
    for (size_t i = 0; i < kCapacity; i++) {
      if (!used_[i] || slots_[i].isWrite() || slots_[i].reg != cmd.reg)
        continue;
      if (cmd.source == CommandSource::Verify &&
          slots_[i].source == CommandSource::Verify) {
        // Refresh the verification target with the newest expectation.
        if (superseded)
          *superseded = slots_[i];
        slots_[i] = cmd;
        return EnqueueResult::Replaced;
      }
      if (cmd.source == CommandSource::Poll)
        return EnqueueResult::Duplicate;
      if (cmd.source == CommandSource::Verify &&
          slots_[i].source == CommandSource::Poll) {
        // Upgrade the pending poll into the verification read.
        slots_[i] = cmd;
        return EnqueueResult::Added;
      }
    }
  }

  int slot = freeSlot();
  if (slot < 0) {
    if (cmd.source != CommandSource::Poll) {
      // Evict the oldest poll read to make room for a user/verify command.
      int victim = -1;
      uint32_t oldest = 0;
      for (size_t i = 0; i < kCapacity; i++) {
        if (used_[i] && slots_[i].source == CommandSource::Poll &&
            (victim < 0 || seq_[i] < oldest)) {
          victim = (int)i;
          oldest = seq_[i];
        }
      }
      if (victim >= 0) {
        removeAt((size_t)victim);
        slot = victim;
      }
    }
    if (slot < 0) {
      overflows_++;
      return EnqueueResult::Full;
    }
  }

  slots_[slot] = cmd;
  used_[slot] = true;
  seq_[slot] = ++seqCounter_;
  count_++;
  if (count_ > highWater_)
    highWater_ = (uint32_t)count_;
  return EnqueueResult::Added;
}

bool CommandQueue::popNext(Command &out) {
  int best = -1;
  for (size_t i = 0; i < kCapacity; i++) {
    if (!used_[i])
      continue;
    if (best < 0) {
      best = (int)i;
      continue;
    }
    uint8_t pi = priorityOf(slots_[i].source);
    uint8_t pb = priorityOf(slots_[best].source);
    if (pi > pb || (pi == pb && seq_[i] < seq_[best]))
      best = (int)i;
  }
  if (best < 0)
    return false;
  out = slots_[best];
  removeAt((size_t)best);
  return true;
}

bool CommandQueue::hasPending(CommandSource src) const {
  for (size_t i = 0; i < kCapacity; i++)
    if (used_[i] && slots_[i].source == src)
      return true;
  return false;
}

bool CommandQueue::containsRead(uint16_t reg) const {
  for (size_t i = 0; i < kCapacity; i++)
    if (used_[i] && !slots_[i].isWrite() && slots_[i].reg == reg)
      return true;
  return false;
}

size_t CommandQueue::removeBySource(CommandSource src, Visitor cb, void *ctx) {
  size_t n = 0;
  for (size_t i = 0; i < kCapacity; i++) {
    if (used_[i] && slots_[i].source == src) {
      if (cb)
        cb(slots_[i], ctx);
      removeAt(i);
      n++;
    }
  }
  return n;
}

size_t CommandQueue::expire(uint32_t now, uint32_t maxAgeMs, Visitor cb,
                            void *ctx) {
  size_t n = 0;
  for (size_t i = 0; i < kCapacity; i++) {
    if (used_[i] && (uint32_t)(now - slots_[i].createdAt) > maxAgeMs) {
      if (cb)
        cb(slots_[i], ctx);
      removeAt(i);
      n++;
    }
  }
  return n;
}

void CommandQueue::clear() {
  for (size_t i = 0; i < kCapacity; i++)
    removeAt(i);
}

} // namespace edk
