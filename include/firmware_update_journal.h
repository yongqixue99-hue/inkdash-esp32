#pragma once

#include <Arduino.h>
#include <esp_partition.h>

#include "firmware_journal_record.h"

namespace inkdash {

class FirmwareUpdateJournal {
 public:
  bool begin();
  bool active(FirmwareJournalRecord& output) const;
  bool pending(FirmwareJournalRecord& output) const;
  bool awaitingHealth(FirmwareJournalRecord& output) const;
  bool savePending(uint8_t target_subtype, uint32_t version_code,
                   uint32_t image_size,
                   const uint8_t (&image_sha256)[32]);
  bool saveAwaitingHealth(const FirmwareJournalRecord& source,
                          uint8_t previous_subtype,
                          uint8_t boot_attempts);
  bool clear();

 private:
  bool readRecord(uint8_t slot, FirmwareJournalRecord& output) const;
  bool commit(const FirmwareJournalRecord& record);

  const esp_partition_t* partition_ = nullptr;
  bool mounted_ = false;
  int8_t active_slot_ = -1;
  FirmwareJournalRecord record_{};
};

}  // namespace inkdash
