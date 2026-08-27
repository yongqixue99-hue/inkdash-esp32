#pragma once

#include <stdint.h>

namespace inkdash {

struct OtaSelectDecision {
  bool valid = false;
  uint8_t sector = 0;
  uint32_t sequence = 0;
};

// Mirrors ESP-IDF's esp_rewrite_ota_data() sequence selection without the
// image_validate() call that precedes it in ESP-IDF 4.4. The caller must have
// already authenticated and hash-verified the complete target image.
inline OtaSelectDecision chooseOtaSelectEntry(int active_sector,
                                              uint32_t active_sequence,
                                              uint8_t target_slot,
                                              uint8_t app_count) {
  if (app_count == 0 || app_count > 16 || target_slot >= app_count ||
      active_sector < -1 || active_sector > 1) {
    return {};
  }
  if (active_sector < 0) {
    OtaSelectDecision decision;
    decision.valid = true;
    decision.sector = 0;
    decision.sequence = static_cast<uint32_t>(target_slot) + 1U;
    return decision;
  }
  if (active_sequence == 0 || active_sequence == UINT32_MAX) {
    return {};
  }

  const uint32_t residue =
      (static_cast<uint32_t>(target_slot) + 1U) % app_count;
  uint32_t candidate = residue;
  while (candidate < active_sequence) {
    if (candidate > UINT32_MAX - app_count) {
      return {};
    }
    candidate += app_count;
  }
  if (candidate == 0) {
    return {};
  }
  OtaSelectDecision decision;
  decision.valid = true;
  decision.sector = static_cast<uint8_t>((~active_sector) & 1);
  decision.sequence = candidate;
  return decision;
}

}  // namespace inkdash
