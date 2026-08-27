#pragma once

#include <Arduino.h>
#include <esp_partition.h>

#include "wifi_config_record.h"

namespace inkdash {

class WifiConfigStore {
 public:
  bool begin();
  bool load(char (&ssid)[33], char (&password)[64]) const;
  bool save(const char* ssid, const char* password);

 private:
  bool readSlot(uint8_t slot, WifiConfigRecord& output) const;
  bool writeCandidate(const WifiConfigRecord& candidate);

  const esp_partition_t* partition_ = nullptr;
  bool mounted_ = false;
  int8_t active_slot_ = -1;
  WifiConfigRecord record_{};
};

}  // namespace inkdash
