#include "wifi_config_store.h"

#include <esp_spi_flash.h>
#include <string.h>

#include "flash_layout.h"

namespace inkdash {
namespace {

constexpr size_t kSlotOffsets[] = {flash::kWifiConfigSlotA,
                                   flash::kWifiConfigSlotB};

}  // namespace

bool WifiConfigStore::begin() {
  partition_ = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                        ESP_PARTITION_SUBTYPE_DATA_SPIFFS,
                                        "spiffs");
  if (partition_ == nullptr || partition_->size < flash::kReservedEnd) {
    Serial.println("Persistent Wi-Fi data partition unavailable");
    partition_ = nullptr;
    return false;
  }
  mounted_ = true;
  WifiConfigRecord first{};
  WifiConfigRecord second{};
  const bool first_read = readSlot(0, first);
  const bool second_read = readSlot(1, second);
  const WifiConfigRecord* selected = newestValidWifiConfig(
      first_read ? &first : nullptr, second_read ? &second : nullptr);
  if (selected == nullptr) {
    active_slot_ = -1;
    record_ = WifiConfigRecord{};
    Serial.println("Persistent Wi-Fi A/B store ready: no profile");
    return true;
  }
  record_ = *selected;
  active_slot_ = selected == &second ? 1 : 0;
  Serial.printf("Persistent Wi-Fi A/B profile restored: sequence=%lu slot=%c\n",
                static_cast<unsigned long>(record_.sequence),
                active_slot_ == 0 ? 'A' : 'B');
  return true;
}

bool WifiConfigStore::load(char (&ssid)[33], char (&password)[64]) const {
  if (!mounted_ || !validWifiConfigRecord(record_)) {
    memset(ssid, 0, sizeof(ssid));
    memset(password, 0, sizeof(password));
    return false;
  }
  strlcpy(ssid, record_.ssid, sizeof(ssid));
  strlcpy(password, record_.password, sizeof(password));
  return true;
}

bool WifiConfigStore::save(const char* ssid, const char* password) {
  if (!mounted_ || ssid == nullptr || password == nullptr) {
    return false;
  }
  if (sameWifiConfig(record_, ssid, password)) {
    return true;
  }
  const uint32_t sequence =
      active_slot_ < 0 || record_.sequence == UINT32_MAX
          ? 1
          : record_.sequence + 1;
  WifiConfigRecord candidate =
      makeWifiConfigRecord(ssid, password, sequence);
  if (!validWifiConfigRecord(candidate)) {
    memset(candidate.password, 0, sizeof(candidate.password));
    return false;
  }
  const bool written = writeCandidate(candidate);
  memset(candidate.password, 0, sizeof(candidate.password));
  return written;
}

bool WifiConfigStore::readSlot(uint8_t slot, WifiConfigRecord& output) const {
  if (!mounted_ || partition_ == nullptr || slot > 1) {
    return false;
  }
  const void* mapped = nullptr;
  spi_flash_mmap_handle_t map_handle = 0;
  const esp_err_t result = esp_partition_mmap(
      partition_, kSlotOffsets[slot], sizeof(output), SPI_FLASH_MMAP_DATA,
      &mapped, &map_handle);
  if (result != ESP_OK || mapped == nullptr) {
    return false;
  }
  memcpy(&output, mapped, sizeof(output));
  spi_flash_munmap(map_handle);
  return validWifiConfigRecord(output);
}

bool WifiConfigStore::writeCandidate(const WifiConfigRecord& candidate) {
  if (!validWifiConfigRecord(candidate)) {
    return false;
  }
  const uint8_t target_slot = active_slot_ == 0 ? 1 : 0;
  const size_t offset = kSlotOffsets[target_slot];
  if (esp_partition_erase_range(partition_, offset, flash::kSectorBytes) !=
          ESP_OK ||
      esp_partition_write(partition_, offset, &candidate, sizeof(candidate)) !=
          ESP_OK) {
    Serial.println("Persistent Wi-Fi A/B write failed");
    return false;
  }
  WifiConfigRecord verified{};
  if (!readSlot(target_slot, verified) ||
      memcmp(&candidate, &verified, sizeof(candidate)) != 0) {
    Serial.println("Persistent Wi-Fi A/B verification failed");
    return false;
  }
  record_ = verified;
  active_slot_ = target_slot;
  Serial.printf("Persistent Wi-Fi A/B profile saved: sequence=%lu slot=%c\n",
                static_cast<unsigned long>(record_.sequence),
                active_slot_ == 0 ? 'A' : 'B');
  return true;
}

}  // namespace inkdash
