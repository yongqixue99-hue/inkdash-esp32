#pragma once

#include <Arduino.h>
#include <esp_partition.h>

#include "dashboard_data.h"
#include "snapshot_record.h"

namespace inkdash {

class DashboardSnapshotStore {
 public:
  bool begin();
  bool loadCodex(CodexDashboardData& output) const;
  bool loadServer(ServerDashboardData& output) const;
  bool saveCodex(const CodexDashboardData& data);
  bool saveServer(const ServerDashboardData& data);

 private:
  bool readSlot(uint8_t slot, DashboardSnapshotRecord& output) const;
  bool writeCandidate(const DashboardSnapshotRecord& candidate);

  const esp_partition_t* partition_ = nullptr;
  bool mounted_ = false;
  int8_t active_slot_ = -1;
  DashboardSnapshotRecord record_{};
};

}  // namespace inkdash
