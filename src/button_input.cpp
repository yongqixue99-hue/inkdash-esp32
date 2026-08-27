#include "button_input.h"

namespace inkdash {

ButtonInput::ButtonInput(uint8_t pin, bool active_low,
                         bool use_internal_pullup, uint32_t debounce_ms)
    : pin_(pin),
      active_low_(active_low),
      use_internal_pullup_(use_internal_pullup),
      debounce_ms_(debounce_ms) {}

void ButtonInput::begin() {
  pinMode(pin_, use_internal_pullup_ ? INPUT_PULLUP : INPUT);
  synchronize();
}

bool ButtonInput::readPressed() const {
  const bool high = digitalRead(pin_) == HIGH;
  return active_low_ ? !high : high;
}

void ButtonInput::synchronize() {
  raw_state_ = readPressed();
  stable_state_ = raw_state_;
  raw_changed_at_ = millis();
}

bool ButtonInput::pollPressed() {
  const bool current_raw = readPressed();
  const uint32_t now = millis();

  if (current_raw != raw_state_) {
    raw_state_ = current_raw;
    raw_changed_at_ = now;
  }

  if (raw_state_ == stable_state_ || now - raw_changed_at_ < debounce_ms_) {
    return false;
  }

  stable_state_ = raw_state_;
  return stable_state_;
}

}  // namespace inkdash
