#include "snapshot_record.h"

#include <stddef.h>
#include <string.h>

namespace inkdash {
namespace {

constexpr uint32_t kSnapshotMagic = 0x48434144;  // "DACH"
constexpr uint16_t kSnapshotVersion = 1;
constexpr uint8_t kKnownFlags = kSnapshotHasCodex | kSnapshotHasServer;

uint32_t checksum(const DashboardSnapshotRecord& record) {
  const auto* bytes = reinterpret_cast<const uint8_t*>(&record);
  uint32_t value = 2166136261u;
  for (size_t index = 0; index < offsetof(DashboardSnapshotRecord, checksum);
       ++index) {
    value ^= bytes[index];
    value *= 16777619u;
  }
  return value;
}

template <size_t Capacity>
bool terminated(const char (&value)[Capacity]) {
  return memchr(value, '\0', Capacity) != nullptr;
}

bool validCodex(const CodexDashboardData& data) {
  if (data.remaining_percent > 100 || data.used_percent > 100 ||
      data.remaining_percent + data.used_percent != 100 ||
      data.reset_at == 0 || data.generated_at == 0 ||
      !terminated(data.reset_date) || !terminated(data.usage_scope)) {
    return false;
  }
  uint64_t total = 0;
  for (size_t index = 0; index < kUsageDayCount; ++index) {
    if (data.daily_day_of_month[index] < 1 ||
        data.daily_day_of_month[index] > 31) {
      return false;
    }
    total += data.daily_tokens[index];
  }
  if (total != data.week_tokens ||
      data.today_tokens != data.daily_tokens[kUsageDayCount - 1]) {
    return false;
  }
  return strcmp(data.usage_scope, "ACCOUNT") != 0 ||
         data.cumulative_tokens >= data.week_tokens;
}

bool validServer(const ServerDashboardData& data) {
  if (!terminated(data.name) || !terminated(data.traffic_period) ||
      !terminated(data.expiry_date) ||
      !terminated(data.traffic_reset_date) || data.cpu_percent > 100 ||
      data.memory_percent > 100 || data.disk_percent > 100) {
    return false;
  }
  if (!data.configured) {
    return true;
  }
  return data.generated_at > 0 && data.traffic_limit_centi_gb > 0 &&
         data.traffic_limit_centi_gb == data.plan_limit_centi_gb;
}

bool sequenceAfter(uint32_t candidate, uint32_t reference) {
  return static_cast<int32_t>(candidate - reference) > 0;
}

}  // namespace

DashboardSnapshotRecord emptySnapshotRecord() {
  return DashboardSnapshotRecord{};
}

void finalizeSnapshotRecord(DashboardSnapshotRecord& record) {
  record.magic = kSnapshotMagic;
  record.version = kSnapshotVersion;
  record.record_size = sizeof(record);
  memset(record.reserved, 0, sizeof(record.reserved));
  record.checksum = 0;
  record.checksum = checksum(record);
}

bool validSnapshotRecord(const DashboardSnapshotRecord& record) {
  if (record.magic != kSnapshotMagic || record.version != kSnapshotVersion ||
      record.record_size != sizeof(record) || record.sequence == 0 ||
      record.flags == 0 || (record.flags & ~kKnownFlags) != 0 ||
      record.checksum != checksum(record)) {
    return false;
  }
  if ((record.flags & kSnapshotHasCodex) != 0 && !validCodex(record.codex)) {
    return false;
  }
  return (record.flags & kSnapshotHasServer) == 0 ||
         validServer(record.server);
}

const DashboardSnapshotRecord* newestValidSnapshot(
    const DashboardSnapshotRecord* first,
    const DashboardSnapshotRecord* second) {
  const bool first_valid = first != nullptr && validSnapshotRecord(*first);
  const bool second_valid = second != nullptr && validSnapshotRecord(*second);
  if (!first_valid) {
    return second_valid ? second : nullptr;
  }
  if (!second_valid) {
    return first;
  }
  return sequenceAfter(second->sequence, first->sequence) ? second : first;
}

}  // namespace inkdash
