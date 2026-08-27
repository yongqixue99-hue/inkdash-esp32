#include "wallpaper_cache.h"

#include <esp_spi_flash.h>
#include <string.h>

#include "board_config.h"
#include "flash_layout.h"
#include "wallpaper_format.h"

namespace inkdash {
namespace {

constexpr size_t kPayloadBytes =
    kWallpaperHeaderBytes + 2 * board::kPlaneBytes;
static_assert(sizeof(WallpaperCacheRecord) + kPayloadBytes <=
                  flash::kWallpaperSlotBytes,
              "Wallpaper cache slot is too small");

bool verifyPartitionBytes(const esp_partition_t* partition, size_t offset,
                          const uint8_t* expected, size_t size) {
  const void* mapped = nullptr;
  spi_flash_mmap_handle_t map_handle = 0;
  if (esp_partition_mmap(partition, offset, size, SPI_FLASH_MMAP_DATA,
                         &mapped, &map_handle) != ESP_OK ||
      mapped == nullptr) {
    return false;
  }
  const bool matches = memcmp(mapped, expected, size) == 0;
  spi_flash_munmap(map_handle);
  return matches;
}

}  // namespace

WallpaperCache::WallpaperCache(size_t slot_a, size_t slot_b,
                               const char* label)
    : slot_offsets_{slot_a, slot_b},
      label_(label == nullptr ? "Network image" : label) {}

bool WallpaperCache::begin() {
  partition_ = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                        ESP_PARTITION_SUBTYPE_DATA_SPIFFS,
                                        "spiffs");
  if (partition_ == nullptr || partition_->size < flash::kReservedEnd) {
    Serial.printf("%s cache partition unavailable\n", label_);
    partition_ = nullptr;
    return false;
  }
  mounted_ = true;
  WallpaperCacheRecord first{};
  WallpaperCacheRecord second{};
  const bool first_read = readRecord(0, first);
  const bool second_read = readRecord(1, second);
  const WallpaperCacheRecord* selected = newestValidWallpaperCacheRecord(
      first_read ? &first : nullptr, second_read ? &second : nullptr,
      kPayloadBytes);
  if (selected == nullptr) {
    active_slot_ = -1;
    record_ = WallpaperCacheRecord{};
    Serial.printf(
        "%s A/B cache ready: empty, reserved=%u bytes future=%u bytes\n",
        label_,
        static_cast<unsigned>(2 * flash::kWallpaperSlotBytes),
        static_cast<unsigned>(flash::kFutureResourceBytes));
    return true;
  }
  record_ = *selected;
  active_slot_ = selected == &second ? 1 : 0;
  Serial.printf("%s A/B cache found: sequence=%lu slot=%c\n", label_,
                static_cast<unsigned long>(record_.sequence),
                active_slot_ == 0 ? 'A' : 'B');
  return true;
}

bool WallpaperCache::load(uint8_t* black_plane, size_t black_size,
                          uint8_t* red_plane, size_t red_size) {
  if (!mounted_ || black_plane == nullptr || red_plane == nullptr ||
      black_size != board::kPlaneBytes || red_size != board::kPlaneBytes) {
    return false;
  }
  const uint8_t preferred = active_slot_ == 1 ? 1 : 0;
  const uint8_t fallback = preferred == 0 ? 1 : 0;
  WallpaperCacheRecord loaded{};
  if (loadSlot(preferred, black_plane, black_size, red_plane, red_size,
               loaded)) {
    record_ = loaded;
    active_slot_ = preferred;
    Serial.printf("%s restored from cache slot %c\n", label_,
                  preferred == 0 ? 'A' : 'B');
    return true;
  }
  if (loadSlot(fallback, black_plane, black_size, red_plane, red_size,
               loaded)) {
    record_ = loaded;
    active_slot_ = fallback;
    Serial.printf("%s restored from fallback cache slot %c\n", label_,
                  fallback == 0 ? 'A' : 'B');
    return true;
  }
  Serial.printf("No valid cached %s image is available\n", label_);
  return false;
}

bool WallpaperCache::save(const uint8_t* wallpaper_header, size_t header_size,
                          const uint8_t* black_plane, size_t black_size,
                          const uint8_t* red_plane, size_t red_size) {
  if (!mounted_ || wallpaper_header == nullptr || black_plane == nullptr ||
      red_plane == nullptr || header_size != kWallpaperHeaderBytes ||
      black_size != board::kPlaneBytes || red_size != board::kPlaneBytes) {
    return false;
  }
  WallpaperHeader parsed{};
  if (!parseWallpaperHeader(wallpaper_header, header_size,
                            board::kDisplayWidth, board::kDisplayHeight,
                            board::kPlaneBytes, parsed) ||
      wallpaperCrc32(black_plane, black_size) != parsed.black_crc32 ||
      wallpaperCrc32(red_plane, red_size) != parsed.red_crc32) {
    return false;
  }
  if (validWallpaperCacheRecord(record_, kPayloadBytes) &&
      record_.black_crc32 == parsed.black_crc32 &&
      record_.red_crc32 == parsed.red_crc32) {
    return true;
  }

  const uint8_t target_slot = active_slot_ == 0 ? 1 : 0;
  const size_t slot_offset = slot_offsets_[target_slot];
  const size_t payload_offset = slot_offset + sizeof(WallpaperCacheRecord);
  if (esp_partition_erase_range(partition_, slot_offset,
                                flash::kWallpaperSlotBytes) != ESP_OK ||
      esp_partition_write(partition_, payload_offset, wallpaper_header,
                          header_size) != ESP_OK ||
      esp_partition_write(partition_, payload_offset + header_size,
                          black_plane, black_size) != ESP_OK ||
      esp_partition_write(partition_, payload_offset + header_size + black_size,
                          red_plane, red_size) != ESP_OK) {
    Serial.printf("%s cache payload write failed; previous slot kept\n",
                  label_);
    return false;
  }
  if (!verifyPartitionBytes(partition_, payload_offset, wallpaper_header,
                            header_size) ||
      !verifyPartitionBytes(partition_, payload_offset + header_size,
                            black_plane, black_size) ||
      !verifyPartitionBytes(partition_,
                            payload_offset + header_size + black_size,
                            red_plane, red_size)) {
    Serial.printf(
        "%s cache payload verification failed; previous slot kept\n",
        label_);
    return false;
  }

  const uint32_t sequence =
      active_slot_ < 0 || record_.sequence == UINT32_MAX
          ? 1
          : record_.sequence + 1;
  const WallpaperCacheRecord candidate = makeWallpaperCacheRecord(
      sequence, kPayloadBytes, parsed.black_crc32, parsed.red_crc32);
  // Commit metadata is written last. A reset before this write leaves the
  // target slot invalid and the previous A/B copy selected.
  if (esp_partition_write(partition_, slot_offset, &candidate,
                          sizeof(candidate)) != ESP_OK) {
    Serial.printf("%s cache commit failed; previous slot kept\n", label_);
    return false;
  }
  WallpaperCacheRecord verified{};
  if (!readRecord(target_slot, verified) ||
      memcmp(&candidate, &verified, sizeof(candidate)) != 0) {
    Serial.printf("%s cache commit verification failed\n", label_);
    return false;
  }
  record_ = verified;
  active_slot_ = target_slot;
  Serial.printf("%s cached: sequence=%lu slot=%c bytes=%u\n", label_,
                static_cast<unsigned long>(record_.sequence),
                active_slot_ == 0 ? 'A' : 'B',
                static_cast<unsigned>(kPayloadBytes));
  return true;
}

bool WallpaperCache::readRecord(uint8_t slot,
                                WallpaperCacheRecord& output) const {
  if (!mounted_ || partition_ == nullptr || slot > 1) {
    return false;
  }
  const void* mapped = nullptr;
  spi_flash_mmap_handle_t map_handle = 0;
  if (esp_partition_mmap(partition_, slot_offsets_[slot], sizeof(output),
                         SPI_FLASH_MMAP_DATA, &mapped, &map_handle) != ESP_OK ||
      mapped == nullptr) {
    return false;
  }
  memcpy(&output, mapped, sizeof(output));
  spi_flash_munmap(map_handle);
  return validWallpaperCacheRecord(output, kPayloadBytes);
}

bool WallpaperCache::loadSlot(uint8_t slot, uint8_t* black_plane,
                              size_t black_size, uint8_t* red_plane,
                              size_t red_size,
                              WallpaperCacheRecord& record) const {
  if (!readRecord(slot, record)) {
    return false;
  }
  const size_t payload_offset =
      slot_offsets_[slot] + sizeof(WallpaperCacheRecord);
  const void* mapped = nullptr;
  spi_flash_mmap_handle_t map_handle = 0;
  if (esp_partition_mmap(partition_, payload_offset, kPayloadBytes,
                         SPI_FLASH_MMAP_DATA, &mapped, &map_handle) != ESP_OK ||
      mapped == nullptr) {
    return false;
  }
  const auto* payload = static_cast<const uint8_t*>(mapped);
  WallpaperHeader parsed{};
  const bool valid = parseWallpaperHeader(
      payload, kWallpaperHeaderBytes, board::kDisplayWidth,
      board::kDisplayHeight, board::kPlaneBytes, parsed);
  if (!valid || parsed.black_crc32 != record.black_crc32 ||
      parsed.red_crc32 != record.red_crc32) {
    spi_flash_munmap(map_handle);
    return false;
  }
  memcpy(black_plane, payload + kWallpaperHeaderBytes, black_size);
  memcpy(red_plane, payload + kWallpaperHeaderBytes + black_size, red_size);
  spi_flash_munmap(map_handle);
  return wallpaperCrc32(black_plane, black_size) == parsed.black_crc32 &&
         wallpaperCrc32(red_plane, red_size) == parsed.red_crc32;
}

}  // namespace inkdash
