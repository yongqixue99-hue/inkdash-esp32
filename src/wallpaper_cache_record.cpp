#include "wallpaper_cache_record.h"

namespace inkdash {

uint32_t wallpaperCacheRecordChecksum(const WallpaperCacheRecord& record) {
  const auto* bytes = reinterpret_cast<const uint8_t*>(&record);
  uint32_t checksum = 2166136261u;
  for (size_t index = 0; index < offsetof(WallpaperCacheRecord, checksum);
       ++index) {
    checksum ^= bytes[index];
    checksum *= 16777619u;
  }
  return checksum;
}

bool validWallpaperCacheRecord(const WallpaperCacheRecord& record,
                               uint32_t expected_payload_size) {
  return record.magic == kWallpaperCacheMagic &&
         record.version == kWallpaperCacheVersion &&
         record.record_size == sizeof(WallpaperCacheRecord) &&
         record.sequence != 0 && record.payload_size == expected_payload_size &&
         record.checksum == wallpaperCacheRecordChecksum(record);
}

WallpaperCacheRecord makeWallpaperCacheRecord(uint32_t sequence,
                                               uint32_t payload_size,
                                               uint32_t black_crc32,
                                               uint32_t red_crc32) {
  WallpaperCacheRecord record{};
  if (sequence == 0 || payload_size == 0) {
    return record;
  }
  record.magic = kWallpaperCacheMagic;
  record.version = kWallpaperCacheVersion;
  record.record_size = sizeof(WallpaperCacheRecord);
  record.sequence = sequence;
  record.payload_size = payload_size;
  record.black_crc32 = black_crc32;
  record.red_crc32 = red_crc32;
  record.checksum = wallpaperCacheRecordChecksum(record);
  return record;
}

const WallpaperCacheRecord* newestValidWallpaperCacheRecord(
    const WallpaperCacheRecord* first, const WallpaperCacheRecord* second,
    uint32_t expected_payload_size) {
  const bool first_valid =
      first != nullptr && validWallpaperCacheRecord(*first, expected_payload_size);
  const bool second_valid = second != nullptr &&
                            validWallpaperCacheRecord(*second,
                                                      expected_payload_size);
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
