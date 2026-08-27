#pragma once

#include <Arduino.h>

namespace inkdash::board {

// 7.5-inch 800x480 tri-color reference board pinout.
constexpr int kEpdChipSelectPin = 7;
constexpr int kEpdDataCommandPin = 2;
constexpr int kEpdResetPin = 8;
constexpr int kEpdBusyPin = 10;
constexpr int kEpdClockPin = 4;
constexpr int kEpdMosiPin = 6;

// Battery measurement circuit from the supplied schematic. GPIO20 (U0RXD)
// drives the high-side measurement switch through Q5/Q6; GPIO0 reads the
// switched 100k/100k divider. Native USB CDC uses GPIO18/19, so GPIO20 is
// available to gate the divider after boot.
constexpr int kBatterySensePin = 0;
constexpr int kBatterySenseEnablePin = 20;
constexpr bool kBatterySenseEnableActiveHigh = true;

// The reference hardware has two capacitive-touch controller outputs:
// KEY3 -> GPIO9 and KEY1 -> GPIO3. InkDash accepts either touch surface
// as a direct page key so both case variants work. The enclosure's separate
// mechanical SW1 remains electrically wired between EN and GND. Firmware
// interprets its following case-like boot through PageStateStore; SW1 itself is
// not a pollable GPIO and requires no wiring change.
constexpr int kPrimaryPageTouchPin = 9;
constexpr int kSecondaryPageTouchPin = 3;
constexpr bool kPageTouchActiveLow = true;
constexpr bool kPageTouchUseInternalPullup = false;
constexpr uint32_t kButtonDebounceMs = 45;

constexpr uint16_t kDisplayWidth = 800;
constexpr uint16_t kDisplayHeight = 480;
constexpr size_t kPlaneBytes =
    static_cast<size_t>(kDisplayWidth) * kDisplayHeight / 8;

}  // namespace inkdash::board
