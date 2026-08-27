#pragma once

#include <stddef.h>
#include <stdint.h>

namespace inkdash {

constexpr uint32_t kWallpaperCacheMagic = 0x434B4E49;
constexpr uint16_t kWallpaperCacheVersion = 1;

struct WallpaperCacheRecord {
  uint32_t magic = 0;
  uint16_t version = 0;
  uint16_t record_size = 0;
  uint32_t sequence = 0;
  uint32_t payload_size = 0;
  uint32_t black_crc32 = 0;
  uint32_t red_crc32 = 0;
  uint32_t checksum = 0;
};

static_assert(sizeof(WallpaperCacheRecord) == 28,
              "Wallpaper cache record layout changed");

uint32_t wallpaperCacheRecordChecksum(const WallpaperCacheRecord& record);
bool validWallpaperCacheRecord(const WallpaperCacheRecord& record,
                               uint32_t expected_payload_size);
WallpaperCacheRecord makeWallpaperCacheRecord(uint32_t sequence,
                                               uint32_t payload_size,
                                               uint32_t black_crc32,
                                               uint32_t red_crc32);
const WallpaperCacheRecord* newestValidWallpaperCacheRecord(
    const WallpaperCacheRecord* first, const WallpaperCacheRecord* second,
    uint32_t expected_payload_size);

}  // namespace inkdash
