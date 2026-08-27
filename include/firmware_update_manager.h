#pragma once

#include <Arduino.h>
#include <esp_partition.h>

#include "battery_monitor.h"
#include "firmware_update_journal.h"

namespace inkdash {

class FirmwareUpdateManager {
 public:
  void begin();
  bool confirmHealthy(bool local_storage_ready);
  void requestCheckNow();
  void process(bool network_connected, const BatteryReading& battery,
               uint32_t now);

 private:
  bool resumePendingActivation(const esp_partition_t* running_partition);
  bool rollbackApplicationImage(const esp_partition_t* running_partition);
  bool checkAndInstall();
  void scheduleRetry(uint32_t now);

  bool running_image_pending_ = false;
  bool application_image_pending_ = false;
  bool bootloader_image_pending_ = false;
  bool check_requested_ = false;
  uint32_t next_check_ms_ = 0;
  FirmwareUpdateJournal journal_;
};

}  // namespace inkdash
