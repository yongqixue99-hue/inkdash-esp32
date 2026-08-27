#pragma once

#include <stddef.h>
#include <stdint.h>

namespace inkdash {

constexpr size_t kWallpaperHeaderBytes = 28;
constexpr uint32_t kWallpaperFormatVersion = 1;

struct WallpaperHeader {
  uint16_t width = 0;
  uint16_t height = 0;
  uint32_t plane_bytes = 0;
  uint32_t black_crc32 = 0;
  uint32_t red_crc32 = 0;
  uint32_t version = 0;
};

bool parseWallpaperHeader(const uint8_t* bytes, size_t size,
                          uint16_t expected_width, uint16_t expected_height,
                          uint32_t expected_plane_bytes,
                          WallpaperHeader& output);
uint32_t wallpaperCrc32(const uint8_t* bytes, size_t size);

}  // namespace inkdash
