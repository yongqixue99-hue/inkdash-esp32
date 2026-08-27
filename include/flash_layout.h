#pragma once

#include <stddef.h>

namespace inkdash::flash {

// The reference 0x150000-byte data partition is intentionally left
// unmounted. InkDash owns fixed raw sectors so every persistent module has a
// disjoint, power-loss-safe A/B area. Keep this layout stable across OTA.
constexpr size_t kSectorBytes = 4096;
constexpr size_t kDataPartitionBytes = 0x150000;

constexpr size_t kDashboardSlotA = 0 * kSectorBytes;
constexpr size_t kDashboardSlotB = 1 * kSectorBytes;
constexpr size_t kPageStateSlotA = 2 * kSectorBytes;
constexpr size_t kPageStateSlotB = 3 * kSectorBytes;
constexpr size_t kWifiConfigSlotA = 4 * kSectorBytes;
constexpr size_t kWifiConfigSlotB = 5 * kSectorBytes;
constexpr size_t kFirmwareJournalSlotA = 6 * kSectorBytes;
constexpr size_t kFirmwareJournalSlotB = 7 * kSectorBytes;

// One 800x480 tri-color wallpaper package is 96,028 bytes. Twenty-four
// sectors provide 98,304 bytes per slot, including the cache record header.
constexpr size_t kWallpaperSlotSectors = 24;
constexpr size_t kWallpaperSlotBytes = kWallpaperSlotSectors * kSectorBytes;
constexpr size_t kWallpaperSlotA = 8 * kSectorBytes;
constexpr size_t kWallpaperSlotB =
    kWallpaperSlotA + kWallpaperSlotBytes;
// The health dashboard is also a server-rendered 800x480 tri-color resource.
// Keep it in its own A/B cache so an interrupted health refresh can never
// damage the user's wallpaper (and vice versa).
constexpr size_t kHealthSlotA = kWallpaperSlotB + kWallpaperSlotBytes;
constexpr size_t kHealthSlotB = kHealthSlotA + kWallpaperSlotBytes;
constexpr size_t kReservedEnd = kHealthSlotB + kWallpaperSlotBytes;
constexpr size_t kFutureResourceBytes = kDataPartitionBytes - kReservedEnd;

static_assert(kReservedEnd <= kDataPartitionBytes,
              "InkDash persistent layout exceeds the data partition");
static_assert(kFutureResourceBytes >= 896 * 1024,
              "Keep at least 896 KiB for future downloadable resources");

}  // namespace inkdash::flash
