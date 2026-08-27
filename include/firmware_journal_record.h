#pragma once

#include <stddef.h>
#include <stdint.h>

namespace inkdash {

constexpr uint32_t kFirmwareJournalMagic = 0x4A4B4E49;
constexpr uint16_t kFirmwareJournalVersion = 1;
constexpr uint8_t kFirmwareJournalClear = 0;
constexpr uint8_t kFirmwareJournalPendingActivation = 1;
constexpr uint8_t kFirmwareJournalAwaitingHealth = 2;
constexpr uint8_t kOta0PartitionSubtype = 0x10;
constexpr uint8_t kOta1PartitionSubtype = 0x11;

struct FirmwareJournalRecord {
  uint32_t magic = 0;
  uint16_t version = 0;
  uint16_t record_size = 0;
  uint32_t sequence = 0;
  uint8_t state = 0;
  uint8_t target_subtype = 0;
  uint8_t previous_subtype = 0;
  uint8_t boot_attempts = 0;
  uint32_t version_code = 0;
  uint32_t image_size = 0;
  uint8_t image_sha256[32]{};
  uint32_t checksum = 0;
};

static_assert(sizeof(FirmwareJournalRecord) == 60,
              "Firmware journal record layout changed");
static_assert(offsetof(FirmwareJournalRecord, checksum) == 56,
              "Firmware journal checksum offset changed");

uint32_t firmwareJournalChecksum(const FirmwareJournalRecord& record);
bool validFirmwareJournalRecord(const FirmwareJournalRecord& record);
bool firmwareJournalActive(const FirmwareJournalRecord& record);
bool firmwareJournalPending(const FirmwareJournalRecord& record);
bool firmwareJournalAwaitingHealth(const FirmwareJournalRecord& record);
FirmwareJournalRecord makePendingFirmwareJournalRecord(
    uint32_t sequence, uint8_t target_subtype, uint32_t version_code,
    uint32_t image_size, const uint8_t (&image_sha256)[32]);
FirmwareJournalRecord makeAwaitingHealthFirmwareJournalRecord(
    uint32_t sequence, uint8_t target_subtype, uint8_t previous_subtype,
    uint8_t boot_attempts, uint32_t version_code, uint32_t image_size,
    const uint8_t (&image_sha256)[32]);
FirmwareJournalRecord makeClearedFirmwareJournalRecord(uint32_t sequence);
const FirmwareJournalRecord* newestValidFirmwareJournalRecord(
    const FirmwareJournalRecord* first, const FirmwareJournalRecord* second);

}  // namespace inkdash
