#include "dashboard_client.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <esp_partition.h>
#include <esp_spi_flash.h>
#include <stddef.h>
#include <string.h>
#include <tcpip_adapter.h>

#include "network_config.h"
#include "wifi_recovery_policy.h"

namespace inkdash {
namespace {

bool validPercent(int value) { return value >= 0 && value <= 100; }

constexpr uint32_t kLegacyWifiConfigMagic = 0x574B4E49;
constexpr uint16_t kLegacyWifiConfigVersion = 1;
constexpr size_t kLegacyWifiConfigSectorSize = 4096;

struct LegacyWifiConfigRecord {
  uint32_t magic;
  uint16_t version;
  uint8_t ssid_length;
  uint8_t password_length;
  char ssid[33];
  char password[64];
  uint32_t checksum;
};

static_assert(sizeof(LegacyWifiConfigRecord) == 112,
              "Persistent Wi-Fi record layout changed");
static_assert(offsetof(LegacyWifiConfigRecord, checksum) == 108,
              "Persistent Wi-Fi checksum offset changed");

uint32_t legacyWifiConfigChecksum(const LegacyWifiConfigRecord& record) {
  const auto* bytes = reinterpret_cast<const uint8_t*>(&record);
  uint32_t checksum = 2166136261u;
  for (size_t index = 0;
       index < offsetof(LegacyWifiConfigRecord, checksum); ++index) {
    checksum ^= bytes[index];
    checksum *= 16777619u;
  }
  return checksum;
}

bool validLegacyWifiConfig(const LegacyWifiConfigRecord& record) {
  return record.magic == kLegacyWifiConfigMagic &&
         record.version == kLegacyWifiConfigVersion &&
         record.ssid_length >= 1 &&
         record.ssid_length <= 32 && record.password_length <= 63 &&
         record.ssid[record.ssid_length] == '\0' &&
         record.password[record.password_length] == '\0' &&
         record.checksum == legacyWifiConfigChecksum(record);
}

const esp_partition_t* wifiConfigPartition() {
  const esp_partition_t* partition = esp_partition_find_first(
      ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, "ota_0");
  return partition != nullptr &&
                 partition->size >= kLegacyWifiConfigSectorSize
             ? partition
             : nullptr;
}

bool validDate(const char* value) {
  if (value == nullptr || strlen(value) != 10) {
    return false;
  }
  for (size_t index = 0; index < 10; ++index) {
    if (index == 4 || index == 7) {
      if (value[index] != '-') {
        return false;
      }
    } else if (value[index] < '0' || value[index] > '9') {
      return false;
    }
  }
  const int month = (value[5] - '0') * 10 + value[6] - '0';
  const int day = (value[8] - '0') * 10 + value[9] - '0';
  return month >= 1 && month <= 12 && day >= 1 && day <= 31;
}

bool validUsageScope(const char* value) {
  return value != nullptr &&
          (strcmp(value, "WIN") == 0 || strcmp(value, "WIN+MAC") == 0 ||
           strcmp(value, "ACCOUNT") == 0);
}

template <size_t Capacity>
void copyText(char (&destination)[Capacity], const char* source) {
  if (source == nullptr) {
    destination[0] = '\0';
    return;
  }
  strlcpy(destination, source, Capacity);
}

bool loadLegacyStoredWifi(char (&ssid)[33], char (&password)[64]) {
  const esp_partition_t* partition = wifiConfigPartition();
  if (partition == nullptr) {
    Serial.println("Wi-Fi configuration partition not found");
    return false;
  }
  LegacyWifiConfigRecord record{};
  const size_t offset = partition->size - kLegacyWifiConfigSectorSize;
  const void* mapped = nullptr;
  spi_flash_mmap_handle_t map_handle = 0;
  const esp_err_t read_result =
      esp_partition_mmap(partition, offset, sizeof(record), SPI_FLASH_MMAP_DATA,
                         &mapped, &map_handle);
  if (read_result == ESP_OK && mapped != nullptr) {
    memcpy(&record, mapped, sizeof(record));
    spi_flash_munmap(map_handle);
  }
  const bool loaded = read_result == ESP_OK && validLegacyWifiConfig(record);
  if (loaded) {
    strlcpy(ssid, record.ssid, sizeof(ssid));
    strlcpy(password, record.password, sizeof(password));
    Serial.println("Persistent Wi-Fi configuration loaded");
  } else if (read_result != ESP_OK) {
    Serial.printf("Persistent Wi-Fi configuration read failed: %d\n",
                  static_cast<int>(read_result));
  } else {
    Serial.println("Persistent Wi-Fi configuration unavailable");
  }
  memset(record.password, 0, sizeof(record.password));
  if (!loaded) {
    memset(ssid, 0, sizeof(ssid));
    memset(password, 0, sizeof(password));
  }
  return loaded;
}

}  // namespace

bool DashboardClient::begin() {
  char stored_ssid[33]{};
  char stored_password[64]{};
  wifi_storage_ready_ = wifi_store_.begin();
  bool has_stored_wifi = wifi_store_.load(stored_ssid, stored_password);
  if (!has_stored_wifi &&
      loadLegacyStoredWifi(stored_ssid, stored_password)) {
    has_stored_wifi = true;
    if (wifi_store_.save(stored_ssid, stored_password)) {
      Serial.println("Legacy Wi-Fi profile migrated into the OTA-safe A/B store");
    } else {
      wifi_storage_ready_ = false;
      Serial.println("Legacy Wi-Fi migration failed; legacy record retained");
    }
  }
  has_configured_wifi_ = network::kWifiSsid[0] != '\0' || has_stored_wifi;
  // InkDash owns the credential record. Keep the ESP-IDF station config in RAM
  // so the Wi-Fi driver cannot overwrite or contend with that record.
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(true);
  WiFi.setAutoReconnect(true);
  if (network::kWifiSsid[0] != '\0') {
    Serial.printf("Connecting to configured Wi-Fi SSID: %s\n",
                  network::kWifiSsid);
    WiFi.begin(network::kWifiSsid, network::kWifiPassword);
  } else if (has_stored_wifi) {
    Serial.printf("Connecting with InkDash persistent Wi-Fi profile: %s\n",
                  stored_ssid);
    WiFi.begin(stored_ssid, stored_password);
  } else {
    memset(stored_password, 0, sizeof(stored_password));
    startProvisioningPortal();
    return fail(String("Wi-Fi setup portal active: ") + provisioning_ssid_);
  }
  memset(stored_password, 0, sizeof(stored_password));

  if (waitForConnection(network::kConnectTimeoutMs)) {
    was_connected_ = true;
    disconnected_since_ms_ = 0;
    last_error_.clear();
    Serial.print("Wi-Fi connected, device IP: ");
    Serial.println(WiFi.localIP());
    return true;
  }

  last_reconnect_attempt_ms_ = millis();
  disconnected_since_ms_ = last_reconnect_attempt_ms_;
  if (wifiFailureAction(has_configured_wifi_) ==
      WifiFailureAction::kRetrySavedNetwork) {
    return fail("saved Wi-Fi unavailable; background retry active");
  }
  startProvisioningPortal();
  return fail(String("Wi-Fi setup portal active: ") + provisioning_ssid_);
}

bool DashboardClient::process() {
  if (processUsbProvisioning()) {
    return true;
  }
  if (portal_active_) {
    wifi_manager_.process();
  }
  const uint32_t now = millis();
  if (!portal_active_ && has_configured_wifi_ && !connected()) {
    if (disconnected_since_ms_ == 0) {
      disconnected_since_ms_ = now == 0 ? 1 : now;
    }
    const uint32_t disconnected_ms = now - disconnected_since_ms_;
    if (wifiFailureAction(has_configured_wifi_, disconnected_ms,
                          network::kSavedWifiRecoveryPortalDelayMs) ==
        WifiFailureAction::kStartProvisioningPortal) {
      Serial.println(
          "Saved Wi-Fi unavailable for one hour; starting recovery portal");
      startProvisioningPortal();
      return true;
    }
  }
  if (!portal_active_ && has_configured_wifi_ && !connected() &&
      now - last_reconnect_attempt_ms_ >=
          network::kSavedWifiRetryIntervalMs) {
    Serial.println("Retrying saved Wi-Fi in station mode");
    WiFi.disconnect(false, false);
    delay(50);
    WiFi.reconnect();
    last_reconnect_attempt_ms_ = millis();
  }
  const bool now_connected = connected();
  const bool became_connected = now_connected && !was_connected_;
  if (became_connected) {
    if (portal_active_) {
      portal_active_ = false;
      wifi_manager_.stopConfigPortal();
    }
    WiFi.setSleep(true);
    const String connected_ssid = WiFi.SSID();
    const String connected_password = WiFi.psk();
    if (connected_ssid.length() > 0) {
      wifi_storage_ready_ =
          wifi_store_.save(connected_ssid.c_str(), connected_password.c_str());
      if (!wifi_storage_ready_) {
        Serial.println("Connected Wi-Fi profile was not persisted");
      }
    }
    last_error_.clear();
    disconnected_since_ms_ = 0;
    Serial.print("Wi-Fi configured, device IP: ");
    Serial.println(WiFi.localIP());
  }
  was_connected_ = now_connected;
  if (now_connected) {
    disconnected_since_ms_ = 0;
  }
  return became_connected;
}

bool DashboardClient::processUsbProvisioning() {
  bool complete_request = false;
  while (Serial.available() > 0) {
    const int next = Serial.read();
    if (next < 0) {
      break;
    }
    if (next == '\r') {
      continue;
    }
    if (next == '\n') {
      complete_request = true;
      break;
    }
    if (usb_provisioning_request_length_ + 1 <
        kUsbProvisioningRequestCapacity) {
      usb_provisioning_request_[usb_provisioning_request_length_++] =
          static_cast<char>(next);
    } else {
      usb_provisioning_request_overflow_ = true;
    }
  }

  if (!complete_request) {
    return false;
  }
  usb_provisioning_request_[usb_provisioning_request_length_] = '\0';
  if (usb_provisioning_request_overflow_) {
    Serial.println("USB provisioning rejected: request too long");
    resetUsbProvisioningRequest();
    return false;
  }

  constexpr char kPrefix[] = "INKDASH_WIFI_V1 ";
  if (strcmp(usb_provisioning_request_, "INKDASH_STATUS_V1") == 0) {
    resetUsbProvisioningRequest();
    char stored_ssid[33]{};
    char stored_password[64]{};
    bool valid = wifi_store_.load(stored_ssid, stored_password);
    if (!valid) {
      valid = loadLegacyStoredWifi(stored_ssid, stored_password);
    }
    memset(stored_password, 0, sizeof(stored_password));
    Serial.printf("Persistent Wi-Fi config status: %s\n",
                  valid ? "valid" : "invalid");
    return false;
  }
  if (strcmp(usb_provisioning_request_, "INKDASH_REFRESH_V1") == 0) {
    resetUsbProvisioningRequest();
    Serial.println("Refresh request accepted");
    return true;
  }
  if (strcmp(usb_provisioning_request_, "INKDASH_PAGE_NEXT_V1") == 0) {
    resetUsbProvisioningRequest();
    next_page_requested_ = true;
    Serial.println("Page-next request accepted");
    return false;
  }
  if (strcmp(usb_provisioning_request_, "INKDASH_BATTERY_V1") == 0) {
    resetUsbProvisioningRequest();
    battery_sample_requested_ = true;
    Serial.println("Battery-sample request accepted");
    return false;
  }
  if (strcmp(usb_provisioning_request_, "INKDASH_OTA_CHECK_V1") == 0) {
    resetUsbProvisioningRequest();
    firmware_update_requested_ = true;
    Serial.println("OTA check request accepted");
    return false;
  }
  if (strcmp(usb_provisioning_request_, "INKDASH_RESTART_V1") == 0) {
    resetUsbProvisioningRequest();
    Serial.println("Restart request accepted; restarting device");
    Serial.flush();
    delay(100);
    ESP.restart();
    return false;
  }
  if (strncmp(usb_provisioning_request_, kPrefix, strlen(kPrefix)) != 0) {
    resetUsbProvisioningRequest();
    return false;
  }

  JsonDocument document;
  const DeserializationError json_error = deserializeJson(
      document, usb_provisioning_request_ + strlen(kPrefix));
  const char* command = document["cmd"] | "";
  const char* ssid = document["ssid"] | "";
  const char* password = document["password"] | "";
  const size_t ssid_length = strlen(ssid);
  const size_t password_length = strlen(password);
  if (json_error || strcmp(command, "wifi.configure") != 0 ||
      ssid_length == 0 || ssid_length > 32 || password_length > 63 ||
      (password_length > 0 && password_length < 8)) {
    Serial.println("USB provisioning rejected: invalid request");
    document.clear();
    resetUsbProvisioningRequest();
    return false;
  }

  char ssid_copy[33]{};
  char password_copy[64]{};
  strlcpy(ssid_copy, ssid, sizeof(ssid_copy));
  strlcpy(password_copy, password, sizeof(password_copy));
  document.clear();
  resetUsbProvisioningRequest();
  return configureWifiFromUsb(ssid_copy, password_copy);
}

bool DashboardClient::configureWifiFromUsb(const char* ssid, char* password) {
  if (connected() && !portal_active_) {
    memset(password, 0, 64);
    Serial.println("USB provisioning rejected: device is already online");
    return false;
  }

  Serial.printf("USB provisioning received for SSID: %s\n", ssid);
  if (portal_active_) {
    wifi_manager_.stopConfigPortal();
    portal_active_ = false;
  }
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(250);
  was_connected_ = false;
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(true);
  WiFi.setAutoReconnect(true);
  WiFi.begin(ssid, password);
  if (!waitForConnection(network::kConnectTimeoutMs)) {
    memset(password, 0, 64);
    Serial.println("USB provisioning failed: Wi-Fi connection timed out");
    startProvisioningPortal();
    return false;
  }
  has_configured_wifi_ = true;
  const bool saved = wifi_store_.save(ssid, password);
  wifi_storage_ready_ = saved;
  memset(password, 0, 64);
  if (!saved) {
    Serial.println("USB provisioning connected but profile persistence failed");
  }
  was_connected_ = true;
  disconnected_since_ms_ = 0;
  last_error_.clear();
  Serial.print("USB provisioning succeeded; device IP: ");
  Serial.println(WiFi.localIP());
  return true;
}

void DashboardClient::resetUsbProvisioningRequest() {
  memset(usb_provisioning_request_, 0,
         sizeof(usb_provisioning_request_));
  usb_provisioning_request_length_ = 0;
  usb_provisioning_request_overflow_ = false;
}

bool DashboardClient::consumeNextPageRequest() {
  const bool requested = next_page_requested_;
  next_page_requested_ = false;
  return requested;
}

bool DashboardClient::consumeBatterySampleRequest() {
  const bool requested = battery_sample_requested_;
  battery_sample_requested_ = false;
  return requested;
}

bool DashboardClient::consumeFirmwareUpdateRequest() {
  const bool requested = firmware_update_requested_;
  firmware_update_requested_ = false;
  return requested;
}

bool DashboardClient::connected() const {
  return WiFi.status() == WL_CONNECTED;
}

const String& DashboardClient::lastError() const { return last_error_; }

bool DashboardClient::provisioning() const { return portal_active_; }

bool DashboardClient::wifiStorageReady() const {
  return wifi_storage_ready_;
}

const String& DashboardClient::provisioningSsid() const {
  return provisioning_ssid_;
}

bool DashboardClient::lastPayloadStale() const {
  return last_payload_stale_;
}

bool DashboardClient::ensureConnected() {
  if (connected()) {
    disconnected_since_ms_ = 0;
    return true;
  }
  if (portal_active_) {
    wifi_manager_.process();
    return fail(String("waiting for Wi-Fi setup on ") + provisioning_ssid_);
  }

  const uint32_t now = millis();
  if (disconnected_since_ms_ == 0) {
    disconnected_since_ms_ = now == 0 ? 1 : now;
  }
  if (wifiFailureAction(has_configured_wifi_,
                        now - disconnected_since_ms_,
                        network::kSavedWifiRecoveryPortalDelayMs) ==
      WifiFailureAction::kStartProvisioningPortal) {
    startProvisioningPortal();
    return fail(String("Wi-Fi setup portal active: ") + provisioning_ssid_);
  }
  if (millis() - last_reconnect_attempt_ms_ <
      network::kSavedWifiRetryIntervalMs) {
    return fail("saved Wi-Fi unavailable; retry pending");
  }
  Serial.println("Wi-Fi disconnected; attempting saved-network reconnect");
  WiFi.disconnect(false, false);
  delay(50);
  WiFi.reconnect();
  last_reconnect_attempt_ms_ = millis();
  if (waitForConnection(5000)) {
    was_connected_ = true;
    last_error_.clear();
    return true;
  }
  return fail("saved Wi-Fi unavailable; background retry active");
}

bool DashboardClient::waitForConnection(uint32_t timeout_ms) {
  const uint32_t started = millis();
  while (!connected() && millis() - started < timeout_ms) {
    delay(100);
  }
  return connected();
}

void DashboardClient::startProvisioningPortal() {
  if (portal_active_) {
    return;
  }
  const uint32_t suffix = static_cast<uint32_t>(ESP.getEfuseMac());
  char access_point[32];
  snprintf(access_point, sizeof(access_point), "InkDash-Setup-%06lX",
           static_cast<unsigned long>(suffix & 0xFFFFFF));
  provisioning_ssid_ = access_point;

  // The saved-network connection was already attempted by begin() or
  // ensureConnected(). Cancel that attempt before starting the SoftAP. Calling
  // WiFiManager::autoConnect() here would issue a second WiFi.begin() while the
  // ESP32-C3 station driver can still be connecting and intermittently fail
  // with ESP_ERR_WIFI_CONN.
  WiFi.disconnect(false, false);
  delay(100);

  // Modem sleep is useful in station mode, but on this ESP32-C3 board it can
  // prevent the SoftAP DHCP reply from reaching a newly associated client.
  WiFi.setSleep(false);
  wifi_manager_.setConfigPortalBlocking(false);
  wifi_manager_.setConnectTimeout(10);
  wifi_manager_.setConfigPortalTimeout(0);
  wifi_manager_.setBreakAfterConfig(true);
  wifi_manager_.setAPStaticIPConfig(IPAddress(192, 168, 4, 1),
                                    IPAddress(192, 168, 4, 1),
                                    IPAddress(255, 255, 255, 0));
  wifi_manager_.startConfigPortal(provisioning_ssid_.c_str(),
                                  network::kProvisioningPassword);
  portal_active_ = !connected();
  was_connected_ = connected();
  if (portal_active_) {
    const esp_err_t dhcp_stop =
        tcpip_adapter_dhcps_stop(TCPIP_ADAPTER_IF_AP);
    delay(10);
    const esp_err_t dhcp_start =
        tcpip_adapter_dhcps_start(TCPIP_ADAPTER_IF_AP);
    tcpip_adapter_dhcp_status_t dhcp_status = ESP_NETIF_DHCP_INIT;
    const esp_err_t dhcp_status_result = tcpip_adapter_dhcps_get_status(
        TCPIP_ADAPTER_IF_AP, &dhcp_status);
    Serial.printf(
        "SoftAP DHCP restart stop=%d start=%d status_result=%d status=%d\n",
        static_cast<int>(dhcp_stop), static_cast<int>(dhcp_start),
        static_cast<int>(dhcp_status_result), static_cast<int>(dhcp_status));
    Serial.printf("Wi-Fi setup AP: %s (password: %s)\n",
                  provisioning_ssid_.c_str(),
                  network::kProvisioningPassword);
  }
}

bool DashboardClient::fetchJsonWithFallback(const char* primary,
                                            const char* fallback,
                                            String& body) {
  for (uint8_t attempt = 1; attempt <= network::kPrimaryRequestAttempts;
       ++attempt) {
    if (fetchJson(primary, body)) {
      return true;
    }
    if (attempt < network::kPrimaryRequestAttempts && connected()) {
      Serial.printf("Primary dashboard request retry %u/%u\n",
                    static_cast<unsigned>(attempt + 1),
                    static_cast<unsigned>(network::kPrimaryRequestAttempts));
      delay(network::kRequestRetryDelayMs);
    }
  }
  const String primary_error = last_error_;
  if (fallback == nullptr || fallback[0] == '\0' ||
      strcmp(primary, fallback) == 0) {
    return false;
  }
  Serial.printf("Primary dashboard endpoint failed; trying fallback: %s\n",
                fallback);
  if (fetchJson(fallback, body)) {
    return true;
  }
  last_error_ = primary_error + "; fallback: " + last_error_;
  return false;
}

bool DashboardClient::fetchJson(const char* endpoint, String& body) {
  if (!ensureConnected()) {
    return false;
  }

  WiFiClient transport;
  HTTPClient request;
  request.setConnectTimeout(network::kRequestTimeoutMs);
  request.setTimeout(network::kRequestTimeoutMs);
  if (!request.begin(transport, endpoint)) {
    return fail("HTTP client could not open endpoint");
  }
  request.addHeader("Accept", "application/json");
  request.setUserAgent("InkDash-ESP32C3/1");
  const int status = request.GET();
  if (status != HTTP_CODE_OK) {
    request.end();
    return fail(String("dashboard HTTP status ") + status);
  }
  const int announced_size = request.getSize();
  if (announced_size <= 0 ||
      announced_size > static_cast<int>(network::kMaximumResponseBytes)) {
    request.end();
    return fail("dashboard response size is invalid");
  }
  body = request.getString();
  request.end();
  if (body.length() == 0 || body.length() > network::kMaximumResponseBytes) {
    return fail("dashboard response body is invalid");
  }
  return true;
}

bool DashboardClient::fetchCodex(CodexDashboardData& output) {
  String body;
  if (!fetchJsonWithFallback(network::kCodexEndpoint,
                             network::kCodexFallbackEndpoint, body)) {
    return false;
  }

  JsonDocument document;
  const DeserializationError json_error = deserializeJson(document, body);
  body.clear();
  if (json_error) {
    return fail(String("Codex JSON is invalid: ") + json_error.c_str());
  }

  JsonObject root = document.as<JsonObject>();
  const int remaining = root["remaining_percent"] | -1;
  const int used = root["used_percent"] | -1;
  const char* reset_date = root["reset_date"];
  const uint32_t reset_at = root["reset_at"] | uint32_t{0};
  const char* usage_scope = root["usage_scope"];
  const uint64_t today_tokens = root["today_tokens"] | uint64_t{0};
  const uint64_t week_tokens = root["week_tokens"] | uint64_t{0};
  const uint64_t cumulative_tokens =
      root["cumulative_tokens"] | uint64_t{0};
  const uint32_t generated_at = root["generated_at"] | uint32_t{0};
  const uint32_t snapshot_age_seconds =
      root["snapshot_age_seconds"] | uint32_t{0};
  JsonArray daily_dates = root["daily_token_dates"].as<JsonArray>();
  JsonArray daily_tokens = root["daily_tokens"].as<JsonArray>();
  JsonArray daily_centi_yi = root["daily_usage_centi_yi"].as<JsonArray>();

  if (!validPercent(remaining) || !validPercent(used) ||
      remaining + used != 100 || !validDate(reset_date) ||
      reset_at == 0 || !validUsageScope(usage_scope) || generated_at == 0 ||
      daily_dates.size() != kUsageDayCount ||
      daily_tokens.size() != kUsageDayCount ||
      daily_centi_yi.size() != kUsageDayCount) {
    return fail("Codex dashboard fields failed validation");
  }

  CodexDashboardData parsed;
  parsed.remaining_percent = remaining;
  parsed.used_percent = used;
  copyText(parsed.reset_date, reset_date);
  parsed.reset_at = reset_at;
  copyText(parsed.usage_scope, usage_scope);
  parsed.today_tokens = today_tokens;
  parsed.week_tokens = week_tokens;
  parsed.cumulative_tokens = cumulative_tokens;
  parsed.generated_at = generated_at;
  uint64_t computed_week = 0;
  for (size_t index = 0; index < kUsageDayCount; ++index) {
    const char* token_date = daily_dates[index];
    if (!validDate(token_date) ||
        (index > 0 && strcmp(daily_dates[index - 1], token_date) >= 0)) {
      return fail("Codex daily Token date is invalid");
    }
    if (!daily_tokens[index].is<uint64_t>() &&
        !daily_tokens[index].is<uint32_t>()) {
      return fail("Codex daily token value is not an integer");
    }
    const uint64_t tokens = daily_tokens[index].as<uint64_t>();
    const int centi_yi = daily_centi_yi[index] | -1;
    if (centi_yi < 0 || centi_yi > 65535) {
      return fail("Codex daily chart value is outside device range");
    }
    const uint64_t chart_tokens =
        static_cast<uint64_t>(centi_yi) * 1000000ULL;
    const uint64_t chart_difference_tokens =
        chart_tokens >= tokens ? chart_tokens - tokens : tokens - chart_tokens;
    if (chart_difference_tokens > 500000ULL) {
      return fail("Codex daily chart value does not match Token total");
    }
    parsed.daily_day_of_month[index] =
        static_cast<uint8_t>((token_date[8] - '0') * 10 + token_date[9] - '0');
    parsed.daily_tokens[index] = tokens;
    parsed.daily_usage_centi_yi[index] = centi_yi;
    computed_week += tokens;
  }
  if (parsed.today_tokens != parsed.daily_tokens[kUsageDayCount - 1] ||
      parsed.week_tokens != computed_week) {
    return fail("Codex token totals are inconsistent");
  }
  if (strcmp(parsed.usage_scope, "ACCOUNT") == 0 &&
      parsed.cumulative_tokens < parsed.week_tokens) {
    return fail("Codex account cumulative Token total is inconsistent");
  }

  output = parsed;
  last_payload_stale_ =
      snapshot_age_seconds > network::kCodexSnapshotFreshAgeSeconds;
  last_error_.clear();
  Serial.printf(
      "Codex dashboard payload validated: %d%% remaining, source=%s\n",
      parsed.remaining_percent, parsed.usage_scope);
  return true;
}

bool DashboardClient::fetchServer(ServerDashboardData& output) {
  String body;
  if (!fetchJsonWithFallback(network::kServerEndpoint,
                             network::kServerFallbackEndpoint, body)) {
    return false;
  }

  JsonDocument document;
  const DeserializationError json_error = deserializeJson(document, body);
  body.clear();
  if (json_error) {
    return fail(String("server JSON is invalid: ") + json_error.c_str());
  }

  JsonObject root = document.as<JsonObject>();
  ServerDashboardData parsed;
  copyText(parsed.name, root["name"] | "SERVER");
  parsed.configured = root["configured"] | false;
  copyText(parsed.traffic_period, root["traffic_period"] | "MONTH");
  parsed.traffic_used_centi_gb = root["traffic_used_centi_gb"] | 0U;
  parsed.traffic_limit_centi_gb = root["traffic_limit_centi_gb"] | 0U;
  parsed.plan_limit_centi_gb = root["plan_limit_centi_gb"] | 0U;
  copyText(parsed.expiry_date, root["expiry_date"] | "");
  parsed.expiry_days_remaining = root["expiry_days_remaining"] | 0U;
  copyText(parsed.traffic_reset_date,
           root["traffic_reset_date"] | "");
  parsed.rx_centi_gb = root["rx_centi_gb"] | 0U;
  parsed.tx_centi_gb = root["tx_centi_gb"] | 0U;
  parsed.uptime_days = root["uptime_days"] | 0U;
  parsed.generated_at = root["generated_at"] | 0U;
  const uint32_t snapshot_age_seconds =
      root["snapshot_age_seconds"] | uint32_t{0};
  const int cpu = root["cpu_percent"] | 0;
  const int memory = root["memory_percent"] | 0;
  const int disk = root["disk_percent"] | 0;
  if (!validPercent(cpu) || !validPercent(memory) || !validPercent(disk)) {
    return fail("server percentage is outside 0..100");
  }
  parsed.cpu_percent = cpu;
  parsed.memory_percent = memory;
  parsed.disk_percent = disk;
  if (parsed.configured && parsed.generated_at == 0) {
    return fail("configured server snapshot has no generation time");
  }
  if ((parsed.expiry_date[0] != '\0' && !validDate(parsed.expiry_date)) ||
      (parsed.traffic_reset_date[0] != '\0' &&
       !validDate(parsed.traffic_reset_date))) {
    return fail("server date is not YYYY-MM-DD");
  }
  if (parsed.configured &&
      (!root["traffic_used_centi_gb"].is<uint32_t>() ||
       !root["traffic_limit_centi_gb"].is<uint32_t>() ||
       !root["plan_limit_centi_gb"].is<uint32_t>() ||
       strcmp(parsed.traffic_period, "MONTH") != 0 ||
       parsed.traffic_limit_centi_gb == 0 ||
       parsed.traffic_limit_centi_gb != parsed.plan_limit_centi_gb ||
       parsed.expiry_date[0] == '\0' ||
       parsed.traffic_reset_date[0] == '\0')) {
    return fail("configured server traffic contract is invalid");
  }

  output = parsed;
  last_payload_stale_ =
      snapshot_age_seconds > network::kServerSnapshotFreshAgeSeconds;
  last_error_.clear();
  Serial.printf("Server dashboard payload validated: configured=%s\n",
                parsed.configured ? "yes" : "no");
  return true;
}

bool DashboardClient::fail(const String& message) {
  last_error_ = message;
  Serial.printf("Dashboard data error: %s\n", last_error_.c_str());
  return false;
}

}  // namespace inkdash
