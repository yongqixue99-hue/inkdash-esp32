#include "wallpaper_format.h"

#include <string.h>

namespace inkdash {
namespace {

constexpr uint8_t kMagic[] = {'I', 'N', 'K', 'W', 'A', 'L', 'L', '1'};

uint16_t readLe16(const uint8_t* source) {
  return static_cast<uint16_t>(source[0]) |
         static_cast<uint16_t>(source[1]) << 8;
}

uint32_t readLe32(const uint8_t* source) {
  return static_cast<uint32_t>(source[0]) |
         static_cast<uint32_t>(source[1]) << 8 |
         static_cast<uint32_t>(source[2]) << 16 |
         static_cast<uint32_t>(source[3]) << 24;
}

}  // namespace

bool parseWallpaperHeader(const uint8_t* bytes, size_t size,
                          uint16_t expected_width, uint16_t expected_height,
                          uint32_t expected_plane_bytes,
                          WallpaperHeader& output) {
  if (bytes == nullptr || size != kWallpaperHeaderBytes ||
      memcmp(bytes, kMagic, sizeof(kMagic)) != 0) {
    return false;
  }
  WallpaperHeader parsed;
  parsed.width = readLe16(bytes + 8);
  parsed.height = readLe16(bytes + 10);
  parsed.plane_bytes = readLe32(bytes + 12);
  parsed.black_crc32 = readLe32(bytes + 16);
  parsed.red_crc32 = readLe32(bytes + 20);
  parsed.version = readLe32(bytes + 24);
  if (parsed.version != kWallpaperFormatVersion ||
      parsed.width != expected_width || parsed.height != expected_height ||
      parsed.plane_bytes != expected_plane_bytes) {
    return false;
  }
  output = parsed;
  return true;
}

uint32_t wallpaperCrc32(const uint8_t* bytes, size_t size) {
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t index = 0; index < size; ++index) {
    crc ^= bytes[index];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      const uint32_t mask = -(crc & 1u);
      crc = (crc >> 1u) ^ (0xEDB88320u & mask);
    }
  }
  return ~crc;
}

}  // namespace inkdash
