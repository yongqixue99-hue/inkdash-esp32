#pragma once

#include <Arduino.h>
#include <esp_partition.h>

#include "flash_layout.h"
#include "wallpaper_cache_record.h"

namespace inkdash {

class WallpaperCache {
 public:
  explicit WallpaperCache(
      size_t slot_a = flash::kWallpaperSlotA,
      size_t slot_b = flash::kWallpaperSlotB,
      const char* label = "Wallpaper");

  bool begin();
  bool load(uint8_t* black_plane, size_t black_size, uint8_t* red_plane,
            size_t red_size);
  bool save(const uint8_t* wallpaper_header, size_t header_size,
            const uint8_t* black_plane, size_t black_size,
            const uint8_t* red_plane, size_t red_size);

 private:
  bool readRecord(uint8_t slot, WallpaperCacheRecord& output) const;
  bool loadSlot(uint8_t slot, uint8_t* black_plane, size_t black_size,
                uint8_t* red_plane, size_t red_size,
                WallpaperCacheRecord& record) const;

  const esp_partition_t* partition_ = nullptr;
  size_t slot_offsets_[2]{};
  const char* label_ = "Network image";
  bool mounted_ = false;
  int8_t active_slot_ = -1;
  WallpaperCacheRecord record_{};
};

}  // namespace inkdash
