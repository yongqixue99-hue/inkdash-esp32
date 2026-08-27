#pragma once

#include <stdint.h>

namespace inkdash {

enum class WifiFailureAction {
  kRetrySavedNetwork,
  kStartProvisioningPortal,
};

constexpr WifiFailureAction wifiFailureAction(
    bool has_configured_wifi, uint32_t disconnected_ms = 0,
    uint32_t recovery_portal_delay_ms = UINT32_MAX) {
  return !has_configured_wifi ||
                 disconnected_ms >= recovery_portal_delay_ms
             ? WifiFailureAction::kStartProvisioningPortal
             : WifiFailureAction::kRetrySavedNetwork;
}

}  // namespace inkdash
