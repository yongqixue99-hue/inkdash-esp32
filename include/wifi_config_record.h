#pragma once

#include <stddef.h>
#include <stdint.h>

namespace inkdash {

constexpr uint32_t kWifiConfigMagic = 0x574B4E49;
constexpr uint16_t kWifiConfigVersion = 2;

struct WifiConfigRecord {
  uint32_t magic = 0;
  uint16_t version = 0;
  uint16_t record_size = 0;
  uint32_t sequence = 0;
  uint8_t ssid_length = 0;
  uint8_t password_length = 0;
  char ssid[33]{};
  char password[64]{};
  uint8_t reserved = 0;
  uint32_t checksum = 0;
};

static_assert(sizeof(WifiConfigRecord) == 116,
              "Persistent Wi-Fi record layout changed");
static_assert(offsetof(WifiConfigRecord, checksum) == 112,
              "Persistent Wi-Fi checksum offset changed");

uint32_t wifiConfigChecksum(const WifiConfigRecord& record);
bool validWifiConfigRecord(const WifiConfigRecord& record);
bool sameWifiConfig(const WifiConfigRecord& record, const char* ssid,
                    const char* password);
WifiConfigRecord makeWifiConfigRecord(const char* ssid, const char* password,
                                      uint32_t sequence);
const WifiConfigRecord* newestValidWifiConfig(const WifiConfigRecord* first,
                                              const WifiConfigRecord* second);

}  // namespace inkdash
