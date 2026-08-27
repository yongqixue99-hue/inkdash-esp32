#include "battery_monitor.h"

#include "board_config.h"

namespace inkdash {
namespace {

constexpr uint32_t kSampleIntervalMs = 3UL * 60UL * 60UL * 1000UL;
constexpr uint8_t kSampleCount = 16;
constexpr uint16_t kMinimumValidBatteryMillivolts = 2800;
constexpr uint16_t kMaximumValidBatteryMillivolts = 4400;
constexpr uint16_t kMaximumExternalPowerMillivolts = 5500;
constexpr uint16_t kEmptyBatteryMillivolts = 3000;
constexpr uint16_t kFullBatteryMillivolts = 4200;

// The reference board measures about 2.425 V at GPIO0 for 5.000 V at BAT+
// (ratio 0.485).
// analogReadMilliVolts() applies the ESP32-C3 ADC calibration, so only the
// measured divider ratio is needed here; the fallback raw-ADC correction is
// intentionally not applied a second time.
constexpr uint32_t kDividerRatioThousandths = 485;

uint8_t percentFromMillivolts(uint16_t millivolts) {
  if (millivolts <= kEmptyBatteryMillivolts) {
    return 0;
  }
  if (millivolts >= kFullBatteryMillivolts) {
    return 100;
  }
  return static_cast<uint8_t>(
      (static_cast<uint32_t>(millivolts - kEmptyBatteryMillivolts) * 100U +
       (kFullBatteryMillivolts - kEmptyBatteryMillivolts) / 2U) /
      (kFullBatteryMillivolts - kEmptyBatteryMillivolts));
}

void setSenseEnabled(bool enabled) {
  const bool high = enabled == board::kBatterySenseEnableActiveHigh;
  digitalWrite(board::kBatterySenseEnablePin, high ? HIGH : LOW);
}

}  // namespace

void BatteryMonitor::begin() {
  pinMode(board::kBatterySenseEnablePin, OUTPUT);
  setSenseEnabled(false);
  pinMode(board::kBatterySensePin, INPUT);
  analogReadResolution(12);
  analogSetPinAttenuation(board::kBatterySensePin, ADC_11db);
}

bool BatteryMonitor::refreshIfDue(uint32_t now, bool force) {
  if (!force && has_sampled_ && now - last_sample_ms_ < kSampleIntervalMs) {
    return false;
  }

  setSenseEnabled(true);
  delay(12);
  // Discard the first conversion after switching the divider on so C26 and
  // the ADC sample-and-hold have settled.
  (void)analogRead(board::kBatterySensePin);
  (void)analogReadMilliVolts(board::kBatterySensePin);
  uint32_t adc_raw_sum = 0;
  uint32_t adc_sum_millivolts = 0;
  for (uint8_t sample = 0; sample < kSampleCount; ++sample) {
    adc_raw_sum += analogRead(board::kBatterySensePin);
    adc_sum_millivolts += analogReadMilliVolts(board::kBatterySensePin);
    delay(2);
  }
  setSenseEnabled(false);

  const uint16_t adc_raw = static_cast<uint16_t>(
      (adc_raw_sum + kSampleCount / 2U) / kSampleCount);
  const uint16_t adc_millivolts = static_cast<uint16_t>(
      (adc_sum_millivolts + kSampleCount / 2U) / kSampleCount);
  const uint32_t calibrated_battery_millivolts =
      (static_cast<uint32_t>(adc_millivolts) * 1000U +
       kDividerRatioThousandths / 2U) /
      kDividerRatioThousandths;
  // A physical-board calibration produced a raw-ADC correction factor of
  // 1.777. Use it only if Arduino's calibrated mV path is outside the
  // physically valid battery range.
  const uint32_t raw_calibrated_battery_millivolts = static_cast<uint32_t>(
      (static_cast<uint64_t>(adc_raw) * 3300ULL * 1777ULL +
       4095ULL * 500ULL) /
      (4095ULL * 1000ULL));
  const bool calibrated_valid =
      calibrated_battery_millivolts >= kMinimumValidBatteryMillivolts &&
      calibrated_battery_millivolts <= kMaximumValidBatteryMillivolts;
  const bool raw_calibration_valid =
      raw_calibrated_battery_millivolts >= kMinimumValidBatteryMillivolts &&
      raw_calibrated_battery_millivolts <= kMaximumValidBatteryMillivolts;
  const bool used_raw_calibration = !calibrated_valid && raw_calibration_valid;
  const uint32_t battery_millivolts =
      used_raw_calibration ? raw_calibrated_battery_millivolts
                           : calibrated_battery_millivolts;

  reading_.valid = calibrated_valid || raw_calibration_valid;
  reading_.external_power =
      !reading_.valid &&
      ((calibrated_battery_millivolts > kMaximumValidBatteryMillivolts &&
        calibrated_battery_millivolts <= kMaximumExternalPowerMillivolts) ||
       (raw_calibrated_battery_millivolts > kMaximumValidBatteryMillivolts &&
        raw_calibrated_battery_millivolts <=
            kMaximumExternalPowerMillivolts));
  reading_.millivolts =
      static_cast<uint16_t>(min<uint32_t>(battery_millivolts, 65535U));
  reading_.percent =
      reading_.valid ? percentFromMillivolts(reading_.millivolts) : 0;
  last_sample_ms_ = now;
  has_sampled_ = true;

  if (reading_.valid) {
    Serial.printf(
        "Battery sample: raw=%u ADC=%umV BAT=%umV level=%u%% mode=%s\n",
                  static_cast<unsigned>(adc_raw),
                  static_cast<unsigned>(adc_millivolts),
                  static_cast<unsigned>(reading_.millivolts),
                  static_cast<unsigned>(reading_.percent),
                  used_raw_calibration ? "raw-calibrated" : "calibrated-mV");
  } else if (reading_.external_power) {
    Serial.printf(
        "Battery sample: raw=%u ADC=%umV source=USB calibrated=%lumV raw-calibrated=%lumV\n",
        static_cast<unsigned>(adc_raw),
        static_cast<unsigned>(adc_millivolts),
        static_cast<unsigned long>(calibrated_battery_millivolts),
        static_cast<unsigned long>(raw_calibrated_battery_millivolts));
  } else {
    Serial.printf(
        "Battery sample invalid: raw=%u ADC=%umV calibrated=%lumV raw-calibrated=%lumV\n",
                  static_cast<unsigned>(adc_raw),
                  static_cast<unsigned>(adc_millivolts),
                  static_cast<unsigned long>(calibrated_battery_millivolts),
                  static_cast<unsigned long>(raw_calibrated_battery_millivolts));
  }
  return true;
}

const BatteryReading& BatteryMonitor::reading() const { return reading_; }

}  // namespace inkdash
