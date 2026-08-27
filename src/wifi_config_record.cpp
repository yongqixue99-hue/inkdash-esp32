#include "wifi_config_record.h"

#include <string.h>

namespace inkdash {

uint32_t wifiConfigChecksum(const WifiConfigRecord& record) {
  const auto* bytes = reinterpret_cast<const uint8_t*>(&record);
  uint32_t checksum = 2166136261u;
  for (size_t index = 0; index < offsetof(WifiConfigRecord, checksum); ++index) {
    checksum ^= bytes[index];
    checksum *= 16777619u;
  }
  return checksum;
}

bool validWifiConfigRecord(const WifiConfigRecord& record) {
  return record.magic == kWifiConfigMagic &&
         record.version == kWifiConfigVersion &&
         record.record_size == sizeof(WifiConfigRecord) &&
         record.sequence != 0 && record.ssid_length >= 1 &&
         record.ssid_length <= 32 && record.password_length <= 63 &&
         record.ssid[record.ssid_length] == '\0' &&
         record.password[record.password_length] == '\0' &&
         record.checksum == wifiConfigChecksum(record);
}

bool sameWifiConfig(const WifiConfigRecord& record, const char* ssid,
                    const char* password) {
  return validWifiConfigRecord(record) && ssid != nullptr &&
         password != nullptr && strcmp(record.ssid, ssid) == 0 &&
         strcmp(record.password, password) == 0;
}

WifiConfigRecord makeWifiConfigRecord(const char* ssid, const char* password,
                                      uint32_t sequence) {
  WifiConfigRecord record{};
  if (ssid == nullptr || password == nullptr || strlen(ssid) == 0 ||
      strlen(ssid) > 32 || strlen(password) > 63 || sequence == 0) {
    return record;
  }
  record.magic = kWifiConfigMagic;
  record.version = kWifiConfigVersion;
  record.record_size = sizeof(WifiConfigRecord);
  record.sequence = sequence;
  record.ssid_length = static_cast<uint8_t>(strlen(ssid));
  record.password_length = static_cast<uint8_t>(strlen(password));
  memcpy(record.ssid, ssid, record.ssid_length);
  memcpy(record.password, password, record.password_length);
  record.checksum = wifiConfigChecksum(record);
  return record;
}

const WifiConfigRecord* newestValidWifiConfig(const WifiConfigRecord* first,
                                              const WifiConfigRecord* second) {
  const bool first_valid = first != nullptr && validWifiConfigRecord(*first);
  const bool second_valid = second != nullptr && validWifiConfigRecord(*second);
  if (!first_valid) {
    return second_valid ? second : nullptr;
  }
  if (!second_valid) {
    return first;
  }
  return static_cast<int32_t>(second->sequence - first->sequence) > 0 ? second
                                                                     : first;
}

}  // namespace inkdash
