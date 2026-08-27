#include "firmware_journal_record.h"

#include <string.h>

namespace inkdash {
namespace {

bool shaIsNonzero(const uint8_t (&sha256)[32]) {
  uint8_t combined = 0;
  for (size_t index = 0; index < sizeof(sha256); ++index) {
    combined |= sha256[index];
  }
  return combined != 0;
}

bool validOtaSubtype(uint8_t subtype) {
  return subtype == kOta0PartitionSubtype || subtype == kOta1PartitionSubtype;
}

}  // namespace

uint32_t firmwareJournalChecksum(const FirmwareJournalRecord& record) {
  const auto* bytes = reinterpret_cast<const uint8_t*>(&record);
  uint32_t checksum = 2166136261u;
  for (size_t index = 0; index < offsetof(FirmwareJournalRecord, checksum);
       ++index) {
    checksum ^= bytes[index];
    checksum *= 16777619u;
  }
  return checksum;
}

bool validFirmwareJournalRecord(const FirmwareJournalRecord& record) {
  if (record.magic != kFirmwareJournalMagic ||
      record.version != kFirmwareJournalVersion ||
      record.record_size != sizeof(FirmwareJournalRecord) ||
      record.sequence == 0 ||
      record.checksum != firmwareJournalChecksum(record)) {
    return false;
  }
  if (record.state == kFirmwareJournalClear) {
    uint8_t combined = 0;
    for (size_t index = 0; index < sizeof(record.image_sha256); ++index) {
      combined |= record.image_sha256[index];
    }
    return record.target_subtype == 0 && record.previous_subtype == 0 &&
           record.boot_attempts == 0 && record.version_code == 0 &&
           record.image_size == 0 && combined == 0;
  }
  const bool common_valid =
      validOtaSubtype(record.target_subtype) && record.version_code != 0 &&
      record.image_size >= 65536 && record.image_size <= 0x150000 &&
      shaIsNonzero(record.image_sha256);
  if (record.state == kFirmwareJournalPendingActivation) {
    return common_valid && record.previous_subtype == 0 &&
           record.boot_attempts == 0;
  }
  return record.state == kFirmwareJournalAwaitingHealth && common_valid &&
         validOtaSubtype(record.previous_subtype) &&
         record.previous_subtype != record.target_subtype &&
         record.boot_attempts <= 1;
}

bool firmwareJournalActive(const FirmwareJournalRecord& record) {
  return validFirmwareJournalRecord(record) &&
         record.state != kFirmwareJournalClear;
}

bool firmwareJournalPending(const FirmwareJournalRecord& record) {
  return validFirmwareJournalRecord(record) &&
         record.state == kFirmwareJournalPendingActivation;
}

bool firmwareJournalAwaitingHealth(const FirmwareJournalRecord& record) {
  return validFirmwareJournalRecord(record) &&
         record.state == kFirmwareJournalAwaitingHealth;
}

FirmwareJournalRecord makePendingFirmwareJournalRecord(
    uint32_t sequence, uint8_t target_subtype, uint32_t version_code,
    uint32_t image_size, const uint8_t (&image_sha256)[32]) {
  FirmwareJournalRecord record{};
  if (sequence == 0 ||
      (target_subtype != kOta0PartitionSubtype &&
       target_subtype != kOta1PartitionSubtype) ||
      version_code == 0 || image_size < 65536 || image_size > 0x150000 ||
      !shaIsNonzero(image_sha256)) {
    return record;
  }
  record.magic = kFirmwareJournalMagic;
  record.version = kFirmwareJournalVersion;
  record.record_size = sizeof(FirmwareJournalRecord);
  record.sequence = sequence;
  record.state = kFirmwareJournalPendingActivation;
  record.target_subtype = target_subtype;
  record.version_code = version_code;
  record.image_size = image_size;
  memcpy(record.image_sha256, image_sha256, sizeof(record.image_sha256));
  record.checksum = firmwareJournalChecksum(record);
  return record;
}

FirmwareJournalRecord makeAwaitingHealthFirmwareJournalRecord(
    uint32_t sequence, uint8_t target_subtype, uint8_t previous_subtype,
    uint8_t boot_attempts, uint32_t version_code, uint32_t image_size,
    const uint8_t (&image_sha256)[32]) {
  FirmwareJournalRecord record{};
  if (sequence == 0 || !validOtaSubtype(target_subtype) ||
      !validOtaSubtype(previous_subtype) ||
      previous_subtype == target_subtype || boot_attempts > 1 ||
      version_code == 0 || image_size < 65536 || image_size > 0x150000 ||
      !shaIsNonzero(image_sha256)) {
    return record;
  }
  record.magic = kFirmwareJournalMagic;
  record.version = kFirmwareJournalVersion;
  record.record_size = sizeof(FirmwareJournalRecord);
  record.sequence = sequence;
  record.state = kFirmwareJournalAwaitingHealth;
  record.target_subtype = target_subtype;
  record.previous_subtype = previous_subtype;
  record.boot_attempts = boot_attempts;
  record.version_code = version_code;
  record.image_size = image_size;
  memcpy(record.image_sha256, image_sha256, sizeof(record.image_sha256));
  record.checksum = firmwareJournalChecksum(record);
  return record;
}

FirmwareJournalRecord makeClearedFirmwareJournalRecord(uint32_t sequence) {
  FirmwareJournalRecord record{};
  if (sequence == 0) {
    return record;
  }
  record.magic = kFirmwareJournalMagic;
  record.version = kFirmwareJournalVersion;
  record.record_size = sizeof(FirmwareJournalRecord);
  record.sequence = sequence;
  record.state = kFirmwareJournalClear;
  record.checksum = firmwareJournalChecksum(record);
  return record;
}

const FirmwareJournalRecord* newestValidFirmwareJournalRecord(
    const FirmwareJournalRecord* first, const FirmwareJournalRecord* second) {
  const bool first_valid =
      first != nullptr && validFirmwareJournalRecord(*first);
  const bool second_valid =
      second != nullptr && validFirmwareJournalRecord(*second);
  if (!first_valid) {
    return second_valid ? second : nullptr;
  }
  if (!second_valid) {
    return first;
  }
  return static_cast<int32_t>(second->sequence - first->sequence) > 0 ? second
                                                                     : first;
}

}  // namespace inkdash
