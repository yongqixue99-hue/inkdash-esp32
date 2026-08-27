#pragma once

#include <stdint.h>

namespace inkdash {

enum class OtaFinalizeAction : uint8_t {
  kAccept,
  kVerifyMappedFlash,
  kReject,
};

// Arduino-ESP32 Update.h defines UPDATE_ERROR_READ as 3. This is the only
// finalization failure that may be recovered, and only after the complete
// inactive partition matches the signed SHA-256 through a coherent mmap read.
constexpr uint8_t kArduinoUpdateFlashReadError = 3;

constexpr OtaFinalizeAction otaFinalizeAction(bool update_succeeded,
                                              uint8_t update_error) {
  return update_succeeded
             ? OtaFinalizeAction::kAccept
             : (update_error == kArduinoUpdateFlashReadError
                    ? OtaFinalizeAction::kVerifyMappedFlash
                    : OtaFinalizeAction::kReject);
}

}  // namespace inkdash
