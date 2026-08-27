#include "dashboard_snapshot_store.h"

#include <esp_spi_flash.h>
#include <string.h>

#include "codex_snapshot_persistence.h"
#include "flash_layout.h"

namespace inkdash {
namespace {

constexpr size_t kSlotOffsets[] = {flash::kDashboardSlotA,
                                   flash::kDashboardSlotB};
static_assert(sizeof(DashboardSnapshotRecord) <= flash::kSectorBytes,
              "A dashboard snapshot must fit in one flash sector");

}  // namespace

bool DashboardSnapshotStore::begin() {
  partition_ = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                        ESP_PARTITION_SUBTYPE_DATA_SPIFFS,
                                        "spiffs");
  if (partition_ == nullptr || partition_->size < flash::kReservedEnd) {
    Serial.println("Dashboard snapshot partition unavailable");
    partition_ = nullptr;
    mounted_ = false;
    return false;
  }
  mounted_ = true;

  DashboardSnapshotRecord first{};
  DashboardSnapshotRecord second{};
  const bool first_read = readSlot(0, first);
  const bool second_read = readSlot(1, second);
  const DashboardSnapshotRecord* selected = newestValidSnapshot(
      first_read ? &first : nullptr, second_read ? &second : nullptr);
  if (selected == nullptr) {
    record_ = emptySnapshotRecord();
    active_slot_ = -1;
    Serial.println(
        "Dashboard snapshot storage ready: mapped raw A/B slots, no cache");
    return true;
  }

  record_ = *selected;
  active_slot_ = selected == &second ? 1 : 0;
  Serial.printf(
      "Dashboard snapshot restored: sequence=%lu codex=%s server=%s\n",
      static_cast<unsigned long>(record_.sequence),
      (record_.flags & kSnapshotHasCodex) != 0 ? "yes" : "no",
      (record_.flags & kSnapshotHasServer) != 0 ? "yes" : "no");
  return true;
}

bool DashboardSnapshotStore::loadCodex(CodexDashboardData& output) const {
  if (!mounted_ || (record_.flags & kSnapshotHasCodex) == 0) {
    return false;
  }
  output = record_.codex;
  return true;
}

bool DashboardSnapshotStore::loadServer(ServerDashboardData& output) const {
  if (!mounted_ || (record_.flags & kSnapshotHasServer) == 0) {
    return false;
  }
  output = record_.server;
  return true;
}

bool DashboardSnapshotStore::saveCodex(const CodexDashboardData& data) {
  if (!mounted_) {
    return false;
  }
  if ((record_.flags & kSnapshotHasCodex) != 0 &&
      !codexSnapshotShouldPersist(record_.codex, data)) {
    return true;
  }
  DashboardSnapshotRecord candidate = record_;
  candidate.codex = data;
  candidate.flags |= kSnapshotHasCodex;
  candidate.sequence = candidate.sequence == UINT32_MAX
                           ? 1
                           : candidate.sequence + 1;
  finalizeSnapshotRecord(candidate);
  return writeCandidate(candidate);
}

bool DashboardSnapshotStore::saveServer(const ServerDashboardData& data) {
  if (!mounted_) {
    return false;
  }
  if ((record_.flags & kSnapshotHasServer) != 0 &&
      memcmp(&record_.server, &data, sizeof(data)) == 0) {
    return true;
  }
  DashboardSnapshotRecord candidate = record_;
  candidate.server = data;
  candidate.flags |= kSnapshotHasServer;
  candidate.sequence = candidate.sequence == UINT32_MAX
                           ? 1
                           : candidate.sequence + 1;
  finalizeSnapshotRecord(candidate);
  return writeCandidate(candidate);
}

bool DashboardSnapshotStore::readSlot(
    uint8_t slot, DashboardSnapshotRecord& output) const {
  if (!mounted_ || partition_ == nullptr || slot > 1) {
    return false;
  }
  const void* mapped = nullptr;
  spi_flash_mmap_handle_t map_handle = 0;
  const esp_err_t map_result = esp_partition_mmap(
      partition_, kSlotOffsets[slot], sizeof(output), SPI_FLASH_MMAP_DATA,
      &mapped, &map_handle);
  if (map_result != ESP_OK || mapped == nullptr) {
    return false;
  }
  memcpy(&output, mapped, sizeof(output));
  spi_flash_munmap(map_handle);
  return validSnapshotRecord(output);
}

bool DashboardSnapshotStore::writeCandidate(
    const DashboardSnapshotRecord& candidate) {
  if (!mounted_ || partition_ == nullptr) {
    return false;
  }
  if (!validSnapshotRecord(candidate)) {
    Serial.println("Dashboard snapshot write rejected: candidate validation");
    return false;
  }
  const uint8_t target_slot = active_slot_ == 0 ? 1 : 0;
  const size_t target_offset = kSlotOffsets[target_slot];
  const esp_err_t erase_result = esp_partition_erase_range(
      partition_, target_offset, flash::kSectorBytes);
  if (erase_result != ESP_OK) {
    Serial.printf("Dashboard snapshot write failed: erase error %d\n",
                  static_cast<int>(erase_result));
    return false;
  }
  const esp_err_t write_result = esp_partition_write(
      partition_, target_offset, &candidate, sizeof(candidate));
  if (write_result != ESP_OK) {
    Serial.printf("Dashboard snapshot write failed: flash error %d\n",
                  static_cast<int>(write_result));
    return false;
  }

  DashboardSnapshotRecord verified{};
  if (!readSlot(target_slot, verified)) {
    Serial.println(
        "Dashboard snapshot write failed: mapped readback validation");
    return false;
  }
  if (memcmp(&candidate, &verified, sizeof(candidate)) != 0) {
    const auto* expected = reinterpret_cast<const uint8_t*>(&candidate);
    const auto* actual = reinterpret_cast<const uint8_t*>(&verified);
    size_t first_difference = 0;
    while (first_difference < sizeof(candidate) &&
           expected[first_difference] == actual[first_difference]) {
      ++first_difference;
    }
    Serial.printf(
        "Dashboard snapshot write failed: byte %u expected %02x got %02x\n",
        static_cast<unsigned>(first_difference), expected[first_difference],
        actual[first_difference]);
    return false;
  }
  if (!validSnapshotRecord(verified) ||
      verified.sequence != candidate.sequence) {
    Serial.printf(
        "Dashboard snapshot write failed: record validation magic=%08lx version=%u size=%u sequence=%lu flags=%u\n",
        static_cast<unsigned long>(verified.magic), verified.version,
        verified.record_size, static_cast<unsigned long>(verified.sequence),
        verified.flags);
    return false;
  }
  record_ = verified;
  active_slot_ = target_slot;
  Serial.printf("Dashboard snapshot saved: sequence=%lu slot=%c\n",
                static_cast<unsigned long>(record_.sequence),
                active_slot_ == 0 ? 'A' : 'B');
  return true;
}

}  // namespace inkdash
