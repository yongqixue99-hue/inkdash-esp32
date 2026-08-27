#include <cassert>
#include <cstdint>
#include <cstring>

#include "wallpaper_format.h"

int main() {
  uint8_t bytes[inkdash::kWallpaperHeaderBytes] = {
      'I', 'N', 'K', 'W', 'A', 'L', 'L', '1',
      0x20, 0x03, 0xE0, 0x01, 0x80, 0xBB, 0x00, 0x00,
      0xC0, 0xC8, 0x6D, 0x1E, 0x0A, 0xB8, 0xFB, 0x3D,
      0x01, 0x00, 0x00, 0x00,
  };
  inkdash::WallpaperHeader header;
  assert(inkdash::parseWallpaperHeader(bytes, sizeof(bytes), 800, 480, 48000,
                                       header));
  assert(header.black_crc32 == 0x1E6DC8C0u);
  assert(header.red_crc32 == 0x3DFBB80Au);

  bytes[0] = 'X';
  assert(!inkdash::parseWallpaperHeader(bytes, sizeof(bytes), 800, 480, 48000,
                                        header));
  bytes[0] = 'I';
  bytes[24] = 2;
  assert(!inkdash::parseWallpaperHeader(bytes, sizeof(bytes), 800, 480, 48000,
                                        header));

  const uint8_t sample[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  assert(inkdash::wallpaperCrc32(sample, sizeof(sample)) == 0xCBF43926u);
  return 0;
}
