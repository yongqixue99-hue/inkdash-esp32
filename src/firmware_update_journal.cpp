#include "firmware_update_journal.h"

#include <esp_spi_flash.h>
#include <string.h>

#include "flash_layout.h"

namespace inkdash {
namespace {

constexpr size_t kSlotOffsets[] = {flash::kFirmwareJournalSlotA,
                                   flash::kFirmwareJournalSlotB};
static_assert(sizeof(FirmwareJournalRecord) <= flash::kSectorBytes,
              "Firmware journal record must fit in one flash sector");

}  // namespace

bool FirmwareUpdateJournal::begin() {
  partition_ = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                        ESP_PARTITION_SUBTYPE_DATA_SPIFFS,
                                        "spiffs");
  if (partition_ == nullptr || partition_->size < flash::kReservedEnd) {
    Serial.println("Firmware activation journal unavailable");
    partition_ = nullptr;
    return false;
  }
  mounted_ = true;
  FirmwareJournalRecord first{};
  FirmwareJournalRecord second{};
  const bool first_read = readRecord(0, first);
  const bool second_read = readRecord(1, second);
  const FirmwareJournalRecord* selected = newestValidFirmwareJournalRecord(
      first_read ? &first : nullptr, second_read ? &second : nullptr);
  if (selected == nullptr) {
    active_slot_ = -1;
    record_ = FirmwareJournalRecord{};
    Serial.println("Firmware activation journal ready: empty");
    return true;
  }
  active_slot_ = selected == &second ? 1 : 0;
  record_ = *selected;
  Serial.printf("Firmware activation journal found: sequence=%lu state=%u\n",
                static_cast<unsigned long>(record_.sequence),
                static_cast<unsigned>(record_.state));
  return true;
}

bool FirmwareUpdateJournal::pending(FirmwareJournalRecord& output) const {
  if (!mounted_ || !firmwareJournalPending(record_)) {
    return false;
  }
  output = record_;
  return true;
}

bool FirmwareUpdateJournal::active(FirmwareJournalRecord& output) const {
  if (!mounted_ || !firmwareJournalActive(record_)) {
    return false;
  }
  output = record_;
  return true;
}

bool FirmwareUpdateJournal::awaitingHealth(
    FirmwareJournalRecord& output) const {
  if (!mounted_ || !firmwareJournalAwaitingHealth(record_)) {
    return false;
  }
  output = record_;
  return true;
}

bool FirmwareUpdateJournal::savePending(
    uint8_t target_subtype, uint32_t version_code, uint32_t image_size,
    const uint8_t (&image_sha256)[32]) {
  if (!mounted_) {
    return false;
  }
  const uint32_t sequence =
      active_slot_ < 0 || record_.sequence == UINT32_MAX
          ? 1
          : record_.sequence + 1;
  const FirmwareJournalRecord candidate = makePendingFirmwareJournalRecord(
      sequence, target_subtype, version_code, image_size, image_sha256);
  return validFirmwareJournalRecord(candidate) && commit(candidate);
}

bool FirmwareUpdateJournal::saveAwaitingHealth(
    const FirmwareJournalRecord& source, uint8_t previous_subtype,
    uint8_t boot_attempts) {
  if (!mounted_ || !firmwareJournalActive(source)) {
    return false;
  }
  const uint32_t sequence =
      active_slot_ < 0 || record_.sequence == UINT32_MAX
          ? 1
          : record_.sequence + 1;
  const FirmwareJournalRecord candidate =
      makeAwaitingHealthFirmwareJournalRecord(
          sequence, source.target_subtype, previous_subtype, boot_attempts,
          source.version_code, source.image_size, source.image_sha256);
  return validFirmwareJournalRecord(candidate) && commit(candidate);
}

bool FirmwareUpdateJournal::clear() {
  if (!mounted_) {
    return false;
  }
  if (!firmwareJournalActive(record_)) {
    return true;
  }
  const uint32_t sequence =
      record_.sequence == UINT32_MAX ? 1 : record_.sequence + 1;
  return commit(makeClearedFirmwareJournalRecord(sequence));
}

bool FirmwareUpdateJournal::readRecord(
    uint8_t slot, FirmwareJournalRecord& output) const {
  if (!mounted_ || partition_ == nullptr || slot > 1) {
    return false;
  }
  const void* mapped = nullptr;
  spi_flash_mmap_handle_t handle = 0;
  if (esp_partition_mmap(partition_, kSlotOffsets[slot], sizeof(output),
                         SPI_FLASH_MMAP_DATA, &mapped, &handle) != ESP_OK ||
      mapped == nullptr) {
    return false;
  }
  memcpy(&output, mapped, sizeof(output));
  spi_flash_munmap(handle);
  return true;
}

bool FirmwareUpdateJournal::commit(const FirmwareJournalRecord& record) {
  if (!mounted_ || partition_ == nullptr ||
      !validFirmwareJournalRecord(record)) {
    return false;
  }
  const uint8_t target_slot = active_slot_ == 0 ? 1 : 0;
  const size_t offset = kSlotOffsets[target_slot];
  if (esp_partition_erase_range(partition_, offset, flash::kSectorBytes) !=
          ESP_OK ||
      esp_partition_write(partition_, offset, &record, sizeof(record)) !=
          ESP_OK) {
    Serial.println("Firmware activation journal write failed");
    return false;
  }
  FirmwareJournalRecord verified{};
  if (!readRecord(target_slot, verified) ||
      memcmp(&record, &verified, sizeof(record)) != 0) {
    Serial.println("Firmware activation journal verification failed");
    return false;
  }
  record_ = verified;
  active_slot_ = target_slot;
  Serial.printf("Firmware activation journal saved: sequence=%lu state=%u\n",
                static_cast<unsigned long>(record_.sequence),
                static_cast<unsigned>(record_.state));
  return true;
}

}  // namespace inkdash
