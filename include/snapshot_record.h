#pragma once

#include <stddef.h>
#include <stdint.h>

#include "dashboard_data.h"

namespace inkdash {

constexpr uint8_t kSnapshotHasCodex = 1U << 0;
constexpr uint8_t kSnapshotHasServer = 1U << 1;

struct DashboardSnapshotRecord {
  uint32_t magic = 0;
  uint16_t version = 0;
  uint16_t record_size = 0;
  uint32_t sequence = 0;
  uint8_t flags = 0;
  uint8_t reserved[3]{};
  CodexDashboardData codex{};
  ServerDashboardData server{};
  uint32_t checksum = 0;
};

DashboardSnapshotRecord emptySnapshotRecord();
void finalizeSnapshotRecord(DashboardSnapshotRecord& record);
bool validSnapshotRecord(const DashboardSnapshotRecord& record);
const DashboardSnapshotRecord* newestValidSnapshot(
    const DashboardSnapshotRecord* first,
    const DashboardSnapshotRecord* second);

}  // namespace inkdash
