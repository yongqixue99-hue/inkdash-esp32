#pragma once

#include <stdint.h>

#include "firmware_journal_record.h"

namespace inkdash {

enum class OtaHealthAction : uint8_t {
  kNone,
  kActivateTarget,
  kStartHealthCheck,
  kRollbackToPrevious,
  kCompleteRollback,
  kDiscardJournal,
};

inline OtaHealthAction otaHealthAction(const FirmwareJournalRecord& record,
                                       uint8_t running_subtype) {
  if (!validFirmwareJournalRecord(record) ||
      record.state == kFirmwareJournalClear) {
    return OtaHealthAction::kNone;
  }
  if (record.state == kFirmwareJournalPendingActivation) {
    return running_subtype == record.target_subtype
               ? OtaHealthAction::kDiscardJournal
               : OtaHealthAction::kActivateTarget;
  }
  if (record.state != kFirmwareJournalAwaitingHealth) {
    return OtaHealthAction::kDiscardJournal;
  }
  if (running_subtype == record.target_subtype) {
    return record.boot_attempts == 0
               ? OtaHealthAction::kStartHealthCheck
               : OtaHealthAction::kRollbackToPrevious;
  }
  if (running_subtype == record.previous_subtype) {
    return record.boot_attempts == 0
               ? OtaHealthAction::kActivateTarget
               : OtaHealthAction::kCompleteRollback;
  }
  return OtaHealthAction::kDiscardJournal;
}

}  // namespace inkdash
