#pragma once

#include <Arduino.h>

namespace inkdash {

class ButtonInput {
 public:
  ButtonInput(uint8_t pin, bool active_low, bool use_internal_pullup,
              uint32_t debounce_ms);

  void begin();
  bool pollPressed();
  void synchronize();

 private:
  bool readPressed() const;

  uint8_t pin_;
  bool active_low_;
  bool use_internal_pullup_;
  uint32_t debounce_ms_;
  bool raw_state_ = false;
  bool stable_state_ = false;
  uint32_t raw_changed_at_ = 0;
};

}  // namespace inkdash
