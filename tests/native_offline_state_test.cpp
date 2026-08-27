#include <assert.h>
#include <stdint.h>

#include "codex_display_change.h"
#include "codex_snapshot_persistence.h"
#include "ota_finalize_policy.h"
#include "ota_health_policy.h"
#include "ota_select_policy.h"
#include "snapshot_record.h"
#include "wifi_recovery_policy.h"

int main() {
  using namespace inkdash;

  assert(otaFinalizeAction(true, 0) == OtaFinalizeAction::kAccept);
  assert(otaFinalizeAction(false, 3) ==
         OtaFinalizeAction::kVerifyMappedFlash);
  assert(otaFinalizeAction(false, 1) == OtaFinalizeAction::kReject);
  assert(otaFinalizeAction(false, 2) == OtaFinalizeAction::kReject);
  assert(otaFinalizeAction(false, 7) == OtaFinalizeAction::kReject);

  OtaSelectDecision ota_select = chooseOtaSelectEntry(-1, 0, 0, 2);
  assert(ota_select.valid && ota_select.sector == 0 &&
         ota_select.sequence == 1);
  ota_select = chooseOtaSelectEntry(-1, 0, 1, 2);
  assert(ota_select.valid && ota_select.sector == 0 &&
         ota_select.sequence == 2);
  ota_select = chooseOtaSelectEntry(0, 1, 1, 2);
  assert(ota_select.valid && ota_select.sector == 1 &&
         ota_select.sequence == 2);
  ota_select = chooseOtaSelectEntry(1, 2, 0, 2);
  assert(ota_select.valid && ota_select.sector == 0 &&
         ota_select.sequence == 3);
  ota_select = chooseOtaSelectEntry(0, 3, 1, 2);
  assert(ota_select.valid && ota_select.sector == 1 &&
         ota_select.sequence == 4);
  assert(!chooseOtaSelectEntry(0, 1, 2, 2).valid);
  assert(!chooseOtaSelectEntry(2, 1, 0, 2).valid);
  assert(!chooseOtaSelectEntry(0, UINT32_MAX, 0, 2).valid);

  uint8_t health_sha[32]{};
  health_sha[0] = 1;
  const FirmwareJournalRecord health_pending =
      makePendingFirmwareJournalRecord(1, kOta1PartitionSubtype, 2, 65536,
                                       health_sha);
  assert(otaHealthAction(health_pending, kOta0PartitionSubtype) ==
         OtaHealthAction::kActivateTarget);
  assert(otaHealthAction(health_pending, kOta1PartitionSubtype) ==
         OtaHealthAction::kDiscardJournal);
  const FirmwareJournalRecord health_first_boot =
      makeAwaitingHealthFirmwareJournalRecord(
          2, kOta1PartitionSubtype, kOta0PartitionSubtype, 0, 2, 65536,
          health_sha);
  assert(otaHealthAction(health_first_boot, kOta1PartitionSubtype) ==
         OtaHealthAction::kStartHealthCheck);
  assert(otaHealthAction(health_first_boot, kOta0PartitionSubtype) ==
         OtaHealthAction::kActivateTarget);
  const FirmwareJournalRecord health_failed_boot =
      makeAwaitingHealthFirmwareJournalRecord(
          3, kOta1PartitionSubtype, kOta0PartitionSubtype, 1, 2, 65536,
          health_sha);
  assert(otaHealthAction(health_failed_boot, kOta1PartitionSubtype) ==
         OtaHealthAction::kRollbackToPrevious);
  assert(otaHealthAction(health_failed_boot, kOta0PartitionSubtype) ==
         OtaHealthAction::kCompleteRollback);

  assert(wifiFailureAction(true) ==
         WifiFailureAction::kRetrySavedNetwork);
  assert(wifiFailureAction(false) ==
         WifiFailureAction::kStartProvisioningPortal);
  constexpr uint32_t kRecoveryDelay = 60U * 60U * 1000U;
  assert(wifiFailureAction(true, kRecoveryDelay - 1, kRecoveryDelay) ==
         WifiFailureAction::kRetrySavedNetwork);
  assert(wifiFailureAction(true, kRecoveryDelay, kRecoveryDelay) ==
         WifiFailureAction::kStartProvisioningPortal);

  CodexDashboardData displayed{};
  displayed.remaining_percent = 85;
  displayed.used_percent = 15;
  displayed.reset_at = 1'787'520'000;
  displayed.generated_at = 1'787'490'000;
  displayed.today_tokens = 10;
  displayed.week_tokens = 20;
  displayed.cumulative_tokens = 30;
  CodexDashboardData fetched = displayed;
  fetched.generated_at += 30 * 60;
  assert(!codexDisplayChanged(displayed, DataStatus::kLive, fetched,
                              DataStatus::kLive));
  fetched.remaining_percent = 84;
  fetched.used_percent = 16;
  assert(codexDisplayChanged(displayed, DataStatus::kLive, fetched,
                             DataStatus::kLive));
  fetched = displayed;
  assert(codexDisplayChanged(displayed, DataStatus::kLive, fetched,
                             DataStatus::kStale));
  fetched.generated_at += 24 * 60 * 60;
  assert(codexDisplayChanged(displayed, DataStatus::kLive, fetched,
                             DataStatus::kLive));

  CodexDashboardData stored = displayed;
  stored.generated_at = 10 * 24 * 60 * 60 + 4 * 60 * 60;
  CodexDashboardData latest = stored;
  latest.generated_at += 30 * 60;
  latest.today_tokens += 100'000'000;
  assert(!codexSnapshotShouldPersist(stored, latest));
  latest.remaining_percent = 84;
  latest.used_percent = 16;
  assert(codexSnapshotShouldPersist(stored, latest));
  latest = stored;
  latest.reset_at += 60;
  assert(codexSnapshotShouldPersist(stored, latest));
  latest = stored;
  latest.generated_at += kCodexSnapshotCheckpointSeconds - 1;
  latest.week_tokens += 100'000'000;
  assert(!codexSnapshotShouldPersist(stored, latest));
  latest.generated_at += 1;
  assert(codexSnapshotShouldPersist(stored, latest));

  DashboardSnapshotRecord older = emptySnapshotRecord();
  older.sequence = 41;
  older.flags = kSnapshotHasCodex;
  older.codex.remaining_percent = 17;
  older.codex.used_percent = 83;
  older.codex.reset_at = 100;
  older.codex.generated_at = 200;
  for (size_t index = 0; index < kUsageDayCount; ++index) {
    older.codex.daily_day_of_month[index] =
        static_cast<uint8_t>(index + 1);
  }
  finalizeSnapshotRecord(older);
  assert(validSnapshotRecord(older));

  DashboardSnapshotRecord newer = older;
  newer.sequence = 42;
  newer.codex.remaining_percent = 16;
  newer.codex.used_percent = 84;
  finalizeSnapshotRecord(newer);
  assert(validSnapshotRecord(newer));
  assert(newestValidSnapshot(&older, &newer) == &newer);

  newer.codex.remaining_percent ^= 1;
  assert(!validSnapshotRecord(newer));
  assert(newestValidSnapshot(&older, &newer) == &older);
  assert(newestValidSnapshot(nullptr, &newer) == nullptr);
  return 0;
}
