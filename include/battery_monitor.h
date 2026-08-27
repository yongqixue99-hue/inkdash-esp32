#pragma once

#include <Arduino.h>

namespace inkdash {

struct BatteryReading {
  bool valid = false;
  bool external_power = false;
  uint16_t millivolts = 0;
  uint8_t percent = 0;
};

class BatteryMonitor {
 public:
  void begin();
  bool refreshIfDue(uint32_t now, bool force = false);
  const BatteryReading& reading() const;

 private:
  BatteryReading reading_{};
  uint32_t last_sample_ms_ = 0;
  bool has_sampled_ = false;
};

}  // namespace inkdash
