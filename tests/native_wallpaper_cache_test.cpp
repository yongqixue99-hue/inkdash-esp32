#include <assert.h>
#include <stdint.h>
#include <string.h>

#include <algorithm>
#include <vector>

#include "Arduino.h"
#include "board_config.h"
#include "esp_partition.h"
#include "esp_spi_flash.h"
#include "flash_layout.h"
#include "wallpaper_cache.h"
#include "wallpaper_format.h"

FakeSerialType Serial;

namespace {

esp_partition_t partition{inkdash::flash::kDataPartitionBytes};
std::vector<uint8_t> flash_bytes(partition.size, 0xFF);
size_t stale_partition_read_calls = 0;
size_t mmap_calls = 0;

void writeLe16(uint8_t* output, uint16_t value) {
  output[0] = static_cast<uint8_t>(value);
  output[1] = static_cast<uint8_t>(value >> 8U);
}

void writeLe32(uint8_t* output, uint32_t value) {
  output[0] = static_cast<uint8_t>(value);
  output[1] = static_cast<uint8_t>(value >> 8U);
  output[2] = static_cast<uint8_t>(value >> 16U);
  output[3] = static_cast<uint8_t>(value >> 24U);
}

}  // namespace

const esp_partition_t* esp_partition_find_first(int type, int subtype,
                                                 const char* label) {
  return type == ESP_PARTITION_TYPE_DATA &&
                 subtype == ESP_PARTITION_SUBTYPE_DATA_SPIFFS &&
                 strcmp(label, "spiffs") == 0
             ? &partition
             : nullptr;
}

esp_err_t esp_partition_erase_range(const esp_partition_t*, size_t offset,
                                    size_t size) {
  if (offset + size > flash_bytes.size()) {
    return 1;
  }
  std::fill(flash_bytes.begin() + offset,
            flash_bytes.begin() + offset + size, 0xFF);
  return ESP_OK;
}

esp_err_t esp_partition_write(const esp_partition_t*, size_t offset,
                              const void* source, size_t size) {
  if (source == nullptr || offset + size > flash_bytes.size()) {
    return 1;
  }
  memcpy(flash_bytes.data() + offset, source, size);
  return ESP_OK;
}

// Reproduce the ESP32-C3 symptom seen immediately after a raw partition
// write: this API returned ESP_OK with stale erased bytes, while a mapped read
// exposed the newly written payload.
esp_err_t esp_partition_read(const esp_partition_t*, size_t offset,
                             void* destination, size_t size) {
  if (destination == nullptr || offset + size > flash_bytes.size()) {
    return 1;
  }
  ++stale_partition_read_calls;
  memset(destination, 0xFF, size);
  return ESP_OK;
}

esp_err_t esp_partition_mmap(const esp_partition_t*, size_t offset,
                             size_t size, spi_flash_mmap_memory_t,
                             const void** mapped,
                             spi_flash_mmap_handle_t* handle) {
  if (mapped == nullptr || handle == nullptr ||
      offset + size > flash_bytes.size()) {
    return 1;
  }
  ++mmap_calls;
  *mapped = flash_bytes.data() + offset;
  *handle = mmap_calls;
  return ESP_OK;
}

void spi_flash_munmap(spi_flash_mmap_handle_t) {}

int main() {
  using namespace inkdash;

  std::vector<uint8_t> black(board::kPlaneBytes);
  std::vector<uint8_t> red(board::kPlaneBytes);
  for (size_t index = 0; index < black.size(); ++index) {
    black[index] = static_cast<uint8_t>(index * 17U + 3U);
    red[index] = static_cast<uint8_t>(index * 29U + 11U);
  }

  uint8_t header[kWallpaperHeaderBytes] = {
      'I', 'N', 'K', 'W', 'A', 'L', 'L', '1'};
  writeLe16(header + 8, board::kDisplayWidth);
  writeLe16(header + 10, board::kDisplayHeight);
  writeLe32(header + 12, board::kPlaneBytes);
  writeLe32(header + 16, wallpaperCrc32(black.data(), black.size()));
  writeLe32(header + 20, wallpaperCrc32(red.data(), red.size()));
  writeLe32(header + 24, kWallpaperFormatVersion);

  WallpaperCache cache;
  assert(cache.begin());
  assert(cache.save(header, sizeof(header), black.data(), black.size(),
                    red.data(), red.size()));

  std::vector<uint8_t> loaded_black(board::kPlaneBytes);
  std::vector<uint8_t> loaded_red(board::kPlaneBytes);
  assert(cache.load(loaded_black.data(), loaded_black.size(),
                    loaded_red.data(), loaded_red.size()));
  assert(loaded_black == black);
  assert(loaded_red == red);
  assert(stale_partition_read_calls == 0);
  assert(mmap_calls > 0);

  // A second cache uses disjoint sectors and must not overwrite wallpaper.
  WallpaperCache health_cache(flash::kHealthSlotA, flash::kHealthSlotB,
                              "Health dashboard");
  assert(health_cache.begin());
  black[0] ^= 0x5A;
  red[0] ^= 0xA5;
  writeLe32(header + 16, wallpaperCrc32(black.data(), black.size()));
  writeLe32(header + 20, wallpaperCrc32(red.data(), red.size()));
  assert(health_cache.save(header, sizeof(header), black.data(), black.size(),
                           red.data(), red.size()));
  assert(health_cache.load(loaded_black.data(), loaded_black.size(),
                           loaded_red.data(), loaded_red.size()));
  assert(loaded_black == black);
  assert(loaded_red == red);
  return 0;
}
