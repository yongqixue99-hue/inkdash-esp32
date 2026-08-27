#pragma once

#include <Arduino.h>
#include <WiFiManager.h>

#include "dashboard_data.h"
#include "wifi_config_store.h"

namespace inkdash {

class DashboardClient {
 public:
  bool begin();
  bool process();
  bool fetchCodex(CodexDashboardData& output);
  bool fetchServer(ServerDashboardData& output);
  bool consumeNextPageRequest();
  bool consumeBatterySampleRequest();
  bool consumeFirmwareUpdateRequest();

  const String& lastError() const;
  bool connected() const;
  bool provisioning() const;
  bool wifiStorageReady() const;
  const String& provisioningSsid() const;
  bool lastPayloadStale() const;

 private:
  bool processUsbProvisioning();
  bool configureWifiFromUsb(const char* ssid, char* password);
  void resetUsbProvisioningRequest();
  bool ensureConnected();
  bool waitForConnection(uint32_t timeout_ms);
  void startProvisioningPortal();
  bool fetchJson(const char* endpoint, String& body);
  bool fetchJsonWithFallback(const char* primary, const char* fallback,
                             String& body);
  bool fail(const String& message);

  WiFiManager wifi_manager_;
  WifiConfigStore wifi_store_;
  String last_error_;
  String provisioning_ssid_;
  static constexpr size_t kUsbProvisioningRequestCapacity = 256;
  char usb_provisioning_request_[kUsbProvisioningRequestCapacity]{};
  size_t usb_provisioning_request_length_ = 0;
  bool usb_provisioning_request_overflow_ = false;
  bool portal_active_ = false;
  bool wifi_storage_ready_ = false;
  bool has_configured_wifi_ = false;
  bool was_connected_ = false;
  uint32_t last_reconnect_attempt_ms_ = 0;
  uint32_t disconnected_since_ms_ = 0;
  bool next_page_requested_ = false;
  bool battery_sample_requested_ = false;
  bool firmware_update_requested_ = false;
  bool last_payload_stale_ = false;
};

}  // namespace inkdash
