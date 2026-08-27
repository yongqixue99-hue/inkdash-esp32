#pragma once

#include <string.h>

#include "dashboard_data.h"

namespace inkdash {

// Persist quota changes immediately so the last known allowance survives a
// power loss. Token/chart heartbeats are checkpointed less often to avoid
// erasing the two raw flash slots on every network poll.
constexpr uint32_t kCodexSnapshotCheckpointSeconds = 4U * 60U * 60U;

namespace persistence_detail {

constexpr uint64_t hongKongDay(uint32_t epoch) {
  return (static_cast<uint64_t>(epoch) + 8ULL * 60ULL * 60ULL) /
         (24ULL * 60ULL * 60ULL);
}

}  // namespace persistence_detail

inline bool codexSnapshotShouldPersist(const CodexDashboardData& stored,
                                       const CodexDashboardData& latest) {
  if (stored.remaining_percent != latest.remaining_percent ||
      stored.used_percent != latest.used_percent ||
      stored.reset_at / 60U != latest.reset_at / 60U ||
      memcmp(stored.reset_date, latest.reset_date,
             sizeof(stored.reset_date)) != 0 ||
      persistence_detail::hongKongDay(stored.generated_at) !=
          persistence_detail::hongKongDay(latest.generated_at)) {
    return true;
  }

  if (latest.generated_at <= stored.generated_at) {
    return false;
  }
  return latest.generated_at - stored.generated_at >=
         kCodexSnapshotCheckpointSeconds;
}

}  // namespace inkdash
