#include "firmware_update_manager.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Update.h>
#include <WiFi.h>
#include <bootloader_common.h>
#include <esp_image_format.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_spi_flash.h>
#include <mbedtls/base64.h>
#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>
#include <string.h>

#include "firmware_version.h"
#include "generated/ota_public_key.h"
#include "network_config.h"
#include "ota_finalize_policy.h"
#include "ota_health_policy.h"
#include "ota_select_policy.h"

namespace inkdash {
namespace {

constexpr size_t kMaximumManifestBytes = 4096;
constexpr size_t kMaximumSignedPayloadBytes = 1024;
constexpr size_t kMaximumSignatureBytes = 96;
constexpr size_t kDownloadBufferBytes = 4096;
constexpr uint32_t kFirstCheckDelayMs = 60UL * 1000UL;
constexpr uint32_t kSuccessfulCheckIntervalMs = 24UL * 60UL * 60UL * 1000UL;
constexpr uint32_t kFailedCheckRetryMs = 6UL * 60UL * 60UL * 1000UL;
constexpr uint8_t kMinimumBatteryPercent = 60;
constexpr size_t kMaximumFirmwareBytes = 0x150000;
constexpr size_t kMappedVerificationChunkBytes = 64U * 1024U;
uint8_t ota_download_buffer[kDownloadBufferBytes]{};

struct FirmwareManifest {
  uint32_t version_code = 0;
  char version[33]{};
  uint32_t size = 0;
  uint8_t sha256[32]{};
  char url[192]{};
};

bool due(uint32_t now, uint32_t deadline) {
  return static_cast<int32_t>(now - deadline) >= 0;
}

bool exactKeys(JsonObject object, const char* const* keys, size_t key_count) {
  if (object.size() != key_count) {
    return false;
  }
  for (size_t index = 0; index < key_count; ++index) {
    // Missing members and explicit JSON null both produce a null variant;
    // neither is valid in the signed schema.
    if (object[keys[index]].isNull()) {
      return false;
    }
  }
  return true;
}

int hexNibble(char value) {
  if (value >= '0' && value <= '9') {
    return value - '0';
  }
  if (value >= 'a' && value <= 'f') {
    return value - 'a' + 10;
  }
  if (value >= 'A' && value <= 'F') {
    return value - 'A' + 10;
  }
  return -1;
}

bool parseSha256(const char* text, uint8_t (&output)[32]) {
  if (text == nullptr || strlen(text) != 64) {
    return false;
  }
  for (size_t index = 0; index < sizeof(output); ++index) {
    const int high = hexNibble(text[index * 2]);
    const int low = hexNibble(text[index * 2 + 1]);
    if (high < 0 || low < 0) {
      return false;
    }
    output[index] = static_cast<uint8_t>((high << 4) | low);
  }
  return true;
}

bool decodeBase64(const char* text, uint8_t* output, size_t capacity,
                  size_t& output_size) {
  output_size = 0;
  if (text == nullptr || text[0] == '\0') {
    return false;
  }
  return mbedtls_base64_decode(output, capacity, &output_size,
                               reinterpret_cast<const unsigned char*>(text),
                               strlen(text)) == 0 &&
         output_size > 0;
}

bool verifySignedPayload(const uint8_t* payload, size_t payload_size,
                         const uint8_t* signature, size_t signature_size) {
  uint8_t digest[32]{};
  if (mbedtls_sha256_ret(payload, payload_size, digest, 0) != 0) {
    return false;
  }
  mbedtls_pk_context key;
  mbedtls_pk_init(&key);
  const int parse_result = mbedtls_pk_parse_public_key(
      &key, reinterpret_cast<const unsigned char*>(ota::kReleasePublicKeyPem),
      strlen(ota::kReleasePublicKeyPem) + 1);
  const int verify_result =
      parse_result == 0
          ? mbedtls_pk_verify(&key, MBEDTLS_MD_SHA256, digest, sizeof(digest),
                              signature, signature_size)
          : parse_result;
  mbedtls_pk_free(&key);
  memset(digest, 0, sizeof(digest));
  return verify_result == 0;
}

bool parseManifest(const String& body, FirmwareManifest& output) {
  if (body.length() == 0 || body.length() > kMaximumManifestBytes) {
    return false;
  }
  JsonDocument envelope_document;
  if (deserializeJson(envelope_document, body)) {
    return false;
  }
  JsonObject envelope = envelope_document.as<JsonObject>();
  constexpr const char* kEnvelopeKeys[] = {
      "schema_version", "algorithm", "key_id", "payload", "signature"};
  if (!exactKeys(envelope, kEnvelopeKeys,
                 sizeof(kEnvelopeKeys) / sizeof(kEnvelopeKeys[0])) ||
      (envelope["schema_version"] | 0) != 1 ||
      strcmp(envelope["algorithm"] | "", "ecdsa-p256-sha256") != 0 ||
      strcmp(envelope["key_id"] | "", ota::kReleaseKeyId) != 0) {
    return false;
  }

  uint8_t payload[kMaximumSignedPayloadBytes]{};
  uint8_t signature[kMaximumSignatureBytes]{};
  size_t payload_size = 0;
  size_t signature_size = 0;
  if (!decodeBase64(envelope["payload"] | "", payload, sizeof(payload),
                    payload_size) ||
      !decodeBase64(envelope["signature"] | "", signature,
                    sizeof(signature), signature_size) ||
      !verifySignedPayload(payload, payload_size, signature, signature_size)) {
    memset(payload, 0, sizeof(payload));
    memset(signature, 0, sizeof(signature));
    return false;
  }
  envelope_document.clear();

  JsonDocument payload_document;
  const DeserializationError payload_error =
      deserializeJson(payload_document, payload, payload_size);
  memset(payload, 0, sizeof(payload));
  memset(signature, 0, sizeof(signature));
  if (payload_error) {
    return false;
  }
  JsonObject manifest = payload_document.as<JsonObject>();
  constexpr const char* kPayloadKeys[] = {
      "schema_version", "hardware", "channel", "version_code",
      "version",        "size",     "sha256", "url"};
  const char* version = manifest["version"] | "";
  const char* sha256 = manifest["sha256"] | "";
  const char* url = manifest["url"] | "";
  const uint64_t version_code = manifest["version_code"] | uint64_t{0};
  const uint64_t size = manifest["size"] | uint64_t{0};
  if (!exactKeys(manifest, kPayloadKeys,
                 sizeof(kPayloadKeys) / sizeof(kPayloadKeys[0])) ||
      (manifest["schema_version"] | 0) != 1 ||
      strcmp(manifest["hardware"] | "", firmware::kHardwareId) != 0 ||
      strcmp(manifest["channel"] | "", network::kFirmwareChannel) != 0 ||
      version_code == 0 || version_code > UINT32_MAX || size < 65536 ||
      size > kMaximumFirmwareBytes || strlen(version) == 0 ||
      strlen(version) >= sizeof(output.version) ||
      strlen(url) == 0 || strlen(url) >= sizeof(output.url) ||
      strlen(network::kFirmwareUrlPrefix) == 0 ||
      strncmp(url, network::kFirmwareUrlPrefix,
              strlen(network::kFirmwareUrlPrefix)) != 0 ||
      !parseSha256(sha256, output.sha256)) {
    return false;
  }
  output.version_code = static_cast<uint32_t>(version_code);
  output.size = static_cast<uint32_t>(size);
  strlcpy(output.version, version, sizeof(output.version));
  strlcpy(output.url, url, sizeof(output.url));
  return true;
}

bool fetchManifest(String& body) {
  WiFiClient transport;
  HTTPClient request;
  request.setConnectTimeout(network::kFirmwareRequestTimeoutMs);
  request.setTimeout(network::kFirmwareRequestTimeoutMs);
  if (!request.begin(transport, network::kFirmwareManifestEndpoint)) {
    return false;
  }
  request.addHeader("Accept", "application/vnd.inkdash.ota-manifest+json");
  request.setUserAgent("InkDash-ESP32C3-OTA/1");
  const int status = request.GET();
  const int announced_size = request.getSize();
  if (status != HTTP_CODE_OK || announced_size <= 0 ||
      announced_size > static_cast<int>(kMaximumManifestBytes)) {
    request.end();
    return false;
  }
  body = request.getString();
  request.end();
  return body.length() > 0 && body.length() <= kMaximumManifestBytes;
}

bool mappedPartitionMatches(const esp_partition_t* partition, size_t size,
                            const uint8_t (&expected_sha256)[32]) {
  if (partition == nullptr || size == 0 || size > partition->size) {
    return false;
  }
  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  bool valid = mbedtls_sha256_starts_ret(&sha, 0) == 0;
  size_t offset = 0;
  while (valid && offset < size) {
    const size_t chunk =
        min(kMappedVerificationChunkBytes, size - offset);
    const void* mapped = nullptr;
    spi_flash_mmap_handle_t handle = 0;
    if (esp_partition_mmap(partition, offset, chunk, SPI_FLASH_MMAP_DATA,
                           &mapped, &handle) != ESP_OK ||
        mapped == nullptr) {
      valid = false;
      break;
    }
    const uint8_t* bytes = static_cast<const uint8_t*>(mapped);
    if (offset == 0 && bytes[0] != ESP_IMAGE_HEADER_MAGIC) {
      valid = false;
    } else if (mbedtls_sha256_update_ret(&sha, bytes, chunk) != 0) {
      valid = false;
    }
    spi_flash_munmap(handle);
    offset += chunk;
  }
  uint8_t digest[32]{};
  if (valid && mbedtls_sha256_finish_ret(&sha, digest) != 0) {
    valid = false;
  }
  mbedtls_sha256_free(&sha);
  valid = valid && offset == size &&
          memcmp(digest, expected_sha256, sizeof(digest)) == 0;
  memset(digest, 0, sizeof(digest));
  return valid;
}

bool readMappedOtaEntry(const esp_partition_t* partition, uint8_t sector,
                        esp_ota_select_entry_t& output) {
  if (partition == nullptr || sector > 1 ||
      partition->size < 2 * SPI_FLASH_SEC_SIZE) {
    return false;
  }
  const void* mapped = nullptr;
  spi_flash_mmap_handle_t handle = 0;
  if (esp_partition_mmap(partition, sector * SPI_FLASH_SEC_SIZE,
                         sizeof(output), SPI_FLASH_MMAP_DATA, &mapped,
                         &handle) != ESP_OK ||
      mapped == nullptr) {
    return false;
  }
  memcpy(&output, mapped, sizeof(output));
  spi_flash_munmap(handle);
  return true;
}

esp_err_t selectMappedVerifiedBootPartition(
    const esp_partition_t* target_partition) {
  if (target_partition == nullptr ||
      target_partition->type != ESP_PARTITION_TYPE_APP ||
      target_partition->subtype < ESP_PARTITION_SUBTYPE_APP_OTA_0 ||
      target_partition->subtype >= ESP_PARTITION_SUBTYPE_APP_OTA_MAX) {
    return ESP_ERR_INVALID_ARG;
  }
  const esp_partition_t* ota_data = esp_partition_find_first(
      ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, nullptr);
  if (ota_data == nullptr || ota_data->size < 2 * SPI_FLASH_SEC_SIZE) {
    return ESP_ERR_NOT_FOUND;
  }

  esp_ota_select_entry_t entries[2]{};
  if (!readMappedOtaEntry(ota_data, 0, entries[0]) ||
      !readMappedOtaEntry(ota_data, 1, entries[1])) {
    return ESP_ERR_FLASH_OP_FAIL;
  }
  const uint8_t app_count = esp_ota_get_app_partition_count();
  const int active_sector = bootloader_common_get_active_otadata(entries);
  const uint32_t active_sequence =
      active_sector >= 0 ? entries[active_sector].ota_seq : 0;
  const OtaSelectDecision decision = chooseOtaSelectEntry(
      active_sector, active_sequence,
      static_cast<uint8_t>(target_partition->subtype & 0x0F), app_count);
  if (!decision.valid) {
    return ESP_ERR_OTA_SELECT_INFO_INVALID;
  }

  esp_ota_select_entry_t candidate = entries[decision.sector];
  candidate.ota_seq = decision.sequence;
  candidate.ota_state = ESP_OTA_IMG_NEW;
  candidate.crc = bootloader_common_ota_select_crc(&candidate);
  const size_t offset = decision.sector * SPI_FLASH_SEC_SIZE;
  esp_err_t result =
      esp_partition_erase_range(ota_data, offset, SPI_FLASH_SEC_SIZE);
  if (result == ESP_OK) {
    result = esp_partition_write(ota_data, offset, &candidate,
                                 sizeof(candidate));
  }
  if (result != ESP_OK) {
    return result;
  }

  esp_ota_select_entry_t verified{};
  if (!readMappedOtaEntry(ota_data, decision.sector, verified) ||
      memcmp(&candidate, &verified, sizeof(candidate)) != 0 ||
      !bootloader_common_ota_select_valid(&verified)) {
    return ESP_ERR_FLASH_OP_FAIL;
  }
  return ESP_OK;
}

bool finalizeDownloadedFirmware(const FirmwareManifest& manifest,
                                const esp_partition_t* target_partition,
                                FirmwareUpdateJournal& journal) {
  const bool ended = Update.end(false);
  const uint8_t update_error = Update.getError();
  switch (otaFinalizeAction(ended, update_error)) {
    case OtaFinalizeAction::kAccept:
      return true;
    case OtaFinalizeAction::kReject:
      return false;
    case OtaFinalizeAction::kVerifyMappedFlash:
      break;
  }

  Serial.println(
      "OTA finalizer encountered stale flash read; verifying mapped image");
  if (!mappedPartitionMatches(target_partition, manifest.size,
                              manifest.sha256)) {
    Serial.println("OTA mapped image verification failed");
    return false;
  }
  if (!journal.savePending(static_cast<uint8_t>(target_partition->subtype),
                           manifest.version_code, manifest.size,
                           manifest.sha256)) {
    Serial.println("OTA mapped image verified but journal commit failed");
    return false;
  }
  Serial.println(
      "OTA mapped image verified; restarting for journal activation");
  Serial.flush();
  delay(250);
  ESP.restart();
  return false;
}

bool downloadFirmware(const FirmwareManifest& manifest,
                      FirmwareUpdateJournal& journal) {
  WiFiClient transport;
  HTTPClient request;
  request.setConnectTimeout(network::kFirmwareRequestTimeoutMs);
  request.setTimeout(network::kFirmwareDownloadTimeoutMs);
  if (!request.begin(transport, manifest.url)) {
    return false;
  }
  request.addHeader("Accept", "application/vnd.inkdash.firmware");
  request.setUserAgent("InkDash-ESP32C3-OTA/1");
  const int status = request.GET();
  if (status != HTTP_CODE_OK ||
      request.getSize() != static_cast<int>(manifest.size)) {
    request.end();
    return false;
  }
  const esp_partition_t* target_partition =
      esp_ota_get_next_update_partition(nullptr);
  if (target_partition == nullptr) {
    request.end();
    return false;
  }
  if (!Update.begin(manifest.size, U_FLASH)) {
    request.end();
    return false;
  }

  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  if (mbedtls_sha256_starts_ret(&sha, 0) != 0) {
    mbedtls_sha256_free(&sha);
    Update.abort();
    request.end();
    return false;
  }
  WiFiClient* stream = request.getStreamPtr();
  uint8_t* buffer = ota_download_buffer;
  memset(buffer, 0, kDownloadBufferBytes);
  size_t received = 0;
  uint32_t last_progress_ms = millis();
  bool succeeded = stream != nullptr;
  while (succeeded && received < manifest.size) {
    const size_t remaining = manifest.size - received;
    const size_t wanted = min(kDownloadBufferBytes, remaining);
    const int available = stream->available();
    if (available <= 0) {
      if (!request.connected() ||
          millis() - last_progress_ms > network::kFirmwareDownloadTimeoutMs) {
        succeeded = false;
      } else {
        delay(10);
      }
      continue;
    }
    const size_t chunk = min(wanted, static_cast<size_t>(available));
    const int read = stream->read(buffer, chunk);
    if (read <= 0 ||
        Update.write(buffer, static_cast<size_t>(read)) !=
            static_cast<size_t>(read) ||
        mbedtls_sha256_update_ret(&sha, buffer, static_cast<size_t>(read)) !=
            0) {
      succeeded = false;
      break;
    }
    received += static_cast<size_t>(read);
    last_progress_ms = millis();
    delay(0);
  }
  uint8_t digest[32]{};
  if (succeeded && mbedtls_sha256_finish_ret(&sha, digest) != 0) {
    succeeded = false;
  }
  mbedtls_sha256_free(&sha);
  request.end();
  memset(buffer, 0, kDownloadBufferBytes);
  if (!succeeded || received != manifest.size ||
      memcmp(digest, manifest.sha256, sizeof(digest)) != 0) {
    memset(digest, 0, sizeof(digest));
    Update.abort();
    return false;
  }
  memset(digest, 0, sizeof(digest));
  return finalizeDownloadedFirmware(manifest, target_partition, journal);
}

}  // namespace

void FirmwareUpdateManager::begin() {
  const esp_partition_t* running = esp_ota_get_running_partition();
  application_image_pending_ = false;
  bootloader_image_pending_ = false;
  running_image_pending_ = false;
  if (journal_.begin()) {
    resumePendingActivation(running);
  }
  esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
  bootloader_image_pending_ =
      running != nullptr && esp_ota_get_state_partition(running, &state) ==
                                ESP_OK &&
      state == ESP_OTA_IMG_PENDING_VERIFY;
  running_image_pending_ =
      application_image_pending_ || bootloader_image_pending_;
  Serial.printf("Firmware %s (%lu) running from %s state=%d app_health=%s\n",
                firmware::kVersion,
                static_cast<unsigned long>(firmware::kVersionCode),
                running != nullptr ? running->label : "unknown",
                static_cast<int>(state),
                application_image_pending_ ? "pending" : "clear");
  Serial.printf("Firmware release identity: %s\n", firmware::kReleaseMarker);
#ifdef INKDASH_OTA_ROLLBACK_PROBE
  if (running_image_pending_) {
    Serial.println(
        "OTA rollback probe: rejecting this pending image intentionally");
    Serial.flush();
    delay(250);
    if (application_image_pending_ && rollbackApplicationImage(running)) {
      return;
    }
    const esp_err_t rollback_result =
        esp_ota_mark_app_invalid_rollback_and_reboot();
    Serial.printf("OTA rollback probe could not reboot: %d\n",
                  static_cast<int>(rollback_result));
  }
#endif
  next_check_ms_ = millis() + kFirstCheckDelayMs;
}

bool FirmwareUpdateManager::resumePendingActivation(
    const esp_partition_t* running_partition) {
  FirmwareJournalRecord record{};
  if (!journal_.active(record)) {
    return true;
  }
  if (running_partition == nullptr) {
    Serial.println("OTA journal cannot identify the running slot; clearing");
    journal_.clear();
    return false;
  }
  const uint8_t running_subtype =
      static_cast<uint8_t>(running_partition->subtype);
  const OtaHealthAction action = otaHealthAction(record, running_subtype);
  if (action == OtaHealthAction::kNone) {
    return true;
  }
  if (action == OtaHealthAction::kDiscardJournal) {
    Serial.println("OTA journal state is inconsistent; clearing request");
    journal_.clear();
    return false;
  }
  if (action == OtaHealthAction::kCompleteRollback) {
    const bool cleared = journal_.clear();
    Serial.printf("OTA application rollback completed on %s journal=%s\n",
                  running_partition->label,
                  cleared ? "cleared" : "pending");
    return cleared;
  }
  if (action == OtaHealthAction::kStartHealthCheck) {
    if (!journal_.saveAwaitingHealth(record, record.previous_subtype, 1)) {
      Serial.println("OTA could not record the first health-check boot");
      return false;
    }
    application_image_pending_ = true;
    Serial.printf(
        "OTA application health gate armed: target=%s previous_subtype=0x%02x\n",
        running_partition->label,
        static_cast<unsigned>(record.previous_subtype));
    return true;
  }
  if (action == OtaHealthAction::kRollbackToPrevious) {
    application_image_pending_ = true;
    Serial.println("OTA image restarted before confirmation; rolling back");
    return rollbackApplicationImage(running_partition);
  }

  const esp_partition_t* target = esp_partition_find_first(
      ESP_PARTITION_TYPE_APP,
      static_cast<esp_partition_subtype_t>(record.target_subtype), nullptr);
  if (target == nullptr || target->size < record.image_size) {
    Serial.println("OTA journal target is unavailable; clearing request");
    journal_.clear();
    return false;
  }
  if (!mappedPartitionMatches(target, record.image_size,
                              record.image_sha256)) {
    Serial.println("OTA journal image no longer matches; clearing request");
    journal_.clear();
    return false;
  }
  if (record.state == kFirmwareJournalPendingActivation &&
      !journal_.saveAwaitingHealth(record, running_subtype, 0)) {
    Serial.println("OTA could not arm the application health journal");
    return false;
  }
  // ESP-IDF 4.4's public setter repeats validation through a flash read path
  // that returns stale bytes on this physical board. The signed manifest and
  // full mapped SHA-256 were already verified before the journal was written,
  // and are verified again immediately above. Newer ESP-IDF exposes an
  // official skip-validation setter; this compatibility path mirrors only its
  // power-loss-safe OTA-select update for the older bundled framework.
  const esp_err_t activation = selectMappedVerifiedBootPartition(target);
  if (activation != ESP_OK) {
    Serial.printf("OTA fresh-cache activation failed: %d\n",
                  static_cast<int>(activation));
    return false;
  }
  Serial.printf(
      "OTA fresh-cache activation succeeded: %s health_journal=armed\n",
      target->label);
  Serial.flush();
  delay(250);
  ESP.restart();
  return true;
}

bool FirmwareUpdateManager::rollbackApplicationImage(
    const esp_partition_t* running_partition) {
  FirmwareJournalRecord record{};
  if (running_partition == nullptr || !journal_.awaitingHealth(record) ||
      static_cast<uint8_t>(running_partition->subtype) !=
          record.target_subtype ||
      record.boot_attempts == 0) {
    return false;
  }
  const esp_partition_t* previous = esp_partition_find_first(
      ESP_PARTITION_TYPE_APP,
      static_cast<esp_partition_subtype_t>(record.previous_subtype), nullptr);
  if (previous == nullptr || previous->address == running_partition->address) {
    Serial.println("OTA application rollback slot is unavailable");
    return false;
  }
  const esp_err_t result = selectMappedVerifiedBootPartition(previous);
  if (result != ESP_OK) {
    Serial.printf("OTA application rollback selection failed: %d\n",
                  static_cast<int>(result));
    return false;
  }
  Serial.printf("OTA application rollback selected previous slot: %s\n",
                previous->label);
  Serial.flush();
  delay(250);
  ESP.restart();
  return true;
}

bool FirmwareUpdateManager::confirmHealthy(bool local_storage_ready) {
  if (!running_image_pending_) {
    return true;
  }
  if (!local_storage_ready) {
    Serial.println(
        "OTA image rejected: persistent storage or first-render self-test failed");
    Serial.flush();
    delay(250);
    if (application_image_pending_ &&
        rollbackApplicationImage(esp_ota_get_running_partition())) {
      return false;
    }
    const esp_err_t rollback_result =
        esp_ota_mark_app_invalid_rollback_and_reboot();
    Serial.printf("OTA automatic rollback could not reboot: %d\n",
                  static_cast<int>(rollback_result));
    return false;
  }
  if (application_image_pending_ && !journal_.clear()) {
    Serial.println("OTA application health confirmation could not be saved");
    rollbackApplicationImage(esp_ota_get_running_partition());
    return false;
  }
  esp_err_t result = ESP_OK;
  if (bootloader_image_pending_) {
    result = esp_ota_mark_app_valid_cancel_rollback();
  }
  application_image_pending_ = false;
  bootloader_image_pending_ = result != ESP_OK;
  running_image_pending_ = bootloader_image_pending_;
  Serial.printf("OTA running image health confirmation: %s (%d)\n",
                result == ESP_OK ? "valid" : "failed",
                static_cast<int>(result));
  return result == ESP_OK;
}

void FirmwareUpdateManager::requestCheckNow() {
  check_requested_ = true;
  next_check_ms_ = millis();
}

void FirmwareUpdateManager::process(bool network_connected,
                                    const BatteryReading& battery,
                                    uint32_t now) {
  if (network::kFirmwareManifestEndpoint[0] == '\0' ||
      network::kFirmwareUrlPrefix[0] == '\0') {
    return;
  }
  if (!check_requested_ && !due(now, next_check_ms_)) {
    return;
  }
  // A manual request can only arrive over the USB serial provisioning
  // channel, so Type-C power is physically present even when the board's
  // coarse battery sampler has not classified it yet.
  const bool manual_usb_request = check_requested_;
  if (!network_connected) {
    scheduleRetry(now);
    return;
  }
  const bool safe_power =
      manual_usb_request || battery.external_power ||
      (battery.valid && battery.percent >= kMinimumBatteryPercent);
  if (!safe_power) {
    Serial.println("OTA check deferred: connect Type-C or charge above 60%");
    scheduleRetry(now);
    return;
  }
  check_requested_ = false;
  next_check_ms_ = now + kSuccessfulCheckIntervalMs;
  if (!checkAndInstall()) {
    scheduleRetry(now);
  }
}

bool FirmwareUpdateManager::checkAndInstall() {
  String body;
  if (!fetchManifest(body)) {
    Serial.println("OTA manifest unavailable");
    return false;
  }
  FirmwareManifest manifest{};
  if (!parseManifest(body, manifest)) {
    body.clear();
    Serial.println("OTA manifest signature or fields rejected");
    return false;
  }
  body.clear();
  if (manifest.version_code <= firmware::kVersionCode) {
    Serial.printf("OTA is current: installed=%lu offered=%lu\n",
                  static_cast<unsigned long>(firmware::kVersionCode),
                  static_cast<unsigned long>(manifest.version_code));
    return true;
  }
  Serial.printf("OTA installing %s (%lu), %lu bytes\n", manifest.version,
                static_cast<unsigned long>(manifest.version_code),
                static_cast<unsigned long>(manifest.size));
  if (!downloadFirmware(manifest, journal_)) {
    Serial.printf("OTA download/write rejected: error=%u %s\n",
                  static_cast<unsigned>(Update.getError()),
                  Update.errorString());
    return false;
  }
  Serial.println("OTA verified and activated; software restart pending");
  Serial.flush();
  delay(250);
  ESP.restart();
  return true;
}

void FirmwareUpdateManager::scheduleRetry(uint32_t now) {
  check_requested_ = false;
  next_check_ms_ = now + kFailedCheckRetryMs;
}

}  // namespace inkdash
