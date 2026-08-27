#pragma once

#include <Arduino.h>
#include <esp_partition.h>

#include "page_state_record.h"

namespace inkdash {

struct PageStartupState {
  PageStartupState(size_t selected_page = 0,
                   bool from_case_button = false,
                   bool had_valid_record = false,
                   bool persisted = false,
                   int boot_reset_reason = 0)
      : page_index(selected_page),
        case_button_restart(from_case_button),
        record_was_valid(had_valid_record),
        write_succeeded(persisted),
        reset_reason(boot_reset_reason) {}

  size_t page_index;
  bool case_button_restart;
  bool record_was_valid;
  bool write_succeeded;
  int reset_reason;
};

// Stores the visible page in two checksummed raw sectors. These are sectors 2
// and 3 of the unmounted SPIFFS partition; dashboard snapshots use sectors 0
// and 1. Alternating writes preserve one valid copy if power is interrupted.
class PageStateStore {
 public:
  PageStartupState begin(size_t page_count);
  bool save(size_t page_index);

 private:
  bool readSlot(uint8_t slot, PageStateRecord& output) const;
  bool writeCandidate(const PageStateRecord& candidate);

  const esp_partition_t* partition_ = nullptr;
  size_t page_count_ = 0;
  bool mounted_ = false;
  int8_t active_slot_ = -1;
  PageStateRecord record_{};
};

}  // namespace inkdash
