#include <cassert>
#include <cstring>

#include "firmware_journal_record.h"
#include "flash_layout.h"
#include "wallpaper_cache_record.h"
#include "wifi_config_record.h"

int main() {
  using namespace inkdash;
  static_assert(flash::kDashboardSlotA == 0, "dashboard A moved");
  static_assert(flash::kPageStateSlotA == 2 * flash::kSectorBytes,
                "page state moved");
  static_assert(flash::kWifiConfigSlotA == 4 * flash::kSectorBytes,
                "Wi-Fi store moved");
  static_assert(flash::kFirmwareJournalSlotA == 6 * flash::kSectorBytes,
                "firmware journal moved");
  static_assert(flash::kWallpaperSlotA == 8 * flash::kSectorBytes,
                "wallpaper store moved");
  static_assert(flash::kHealthSlotA ==
                    flash::kWallpaperSlotB + flash::kWallpaperSlotBytes,
                "health cache must follow wallpaper cache");
  static_assert(flash::kFutureResourceBytes >= 896 * 1024,
                "future resource reserve is too small");

  WifiConfigRecord first = makeWifiConfigRecord("FTTR", "password123", 1);
  WifiConfigRecord second = makeWifiConfigRecord("FTTR", "password123", 2);
  assert(validWifiConfigRecord(first));
  assert(sameWifiConfig(first, "FTTR", "password123"));
  assert(newestValidWifiConfig(&first, &second) == &second);
  WifiConfigRecord interrupted_wifi{};
  assert(!validWifiConfigRecord(interrupted_wifi));
  assert(newestValidWifiConfig(&first, &interrupted_wifi) == &first);
  second.password[0] ^= 1;
  assert(!validWifiConfigRecord(second));
  assert(newestValidWifiConfig(&first, &second) == &first);

  WallpaperCacheRecord wallpaper =
      makeWallpaperCacheRecord(7, 96028, 0x12345678, 0xABCDEF01);
  assert(validWallpaperCacheRecord(wallpaper, 96028));
  WallpaperCacheRecord interrupted{};
  assert(!validWallpaperCacheRecord(interrupted, 96028));
  assert(newestValidWallpaperCacheRecord(&wallpaper, &interrupted, 96028) ==
         &wallpaper);
  WallpaperCacheRecord newer =
      makeWallpaperCacheRecord(8, 96028, 0x12345678, 0xABCDEF01);
  assert(newestValidWallpaperCacheRecord(&wallpaper, &newer, 96028) ==
         &newer);
  newer.checksum ^= 1;
  assert(!validWallpaperCacheRecord(newer, 96028));
  assert(newestValidWallpaperCacheRecord(&wallpaper, &newer, 96028) ==
         &wallpaper);
  assert(!validWallpaperCacheRecord(wallpaper, 96027));

  uint8_t image_sha256[32]{};
  for (size_t index = 0; index < sizeof(image_sha256); ++index) {
    image_sha256[index] = static_cast<uint8_t>(index + 1);
  }
  FirmwareJournalRecord pending = makePendingFirmwareJournalRecord(
      9, 0x11, 2026082402, 1'092'400, image_sha256);
  assert(validFirmwareJournalRecord(pending));
  assert(firmwareJournalActive(pending));
  assert(firmwareJournalPending(pending));
  FirmwareJournalRecord awaiting = makeAwaitingHealthFirmwareJournalRecord(
      10, 0x11, 0x10, 0, 2026082402, 1'092'400, image_sha256);
  assert(validFirmwareJournalRecord(awaiting));
  assert(firmwareJournalActive(awaiting));
  assert(firmwareJournalAwaitingHealth(awaiting));
  FirmwareJournalRecord attempted = makeAwaitingHealthFirmwareJournalRecord(
      11, 0x11, 0x10, 1, 2026082402, 1'092'400, image_sha256);
  assert(validFirmwareJournalRecord(attempted));
  assert(firmwareJournalAwaitingHealth(attempted));
  assert(!validFirmwareJournalRecord(makeAwaitingHealthFirmwareJournalRecord(
      12, 0x11, 0x11, 0, 2026082402, 1'092'400, image_sha256)));
  FirmwareJournalRecord cleared = makeClearedFirmwareJournalRecord(10);
  assert(validFirmwareJournalRecord(cleared));
  assert(!firmwareJournalActive(cleared));
  assert(!firmwareJournalPending(cleared));
  assert(newestValidFirmwareJournalRecord(&pending, &cleared) == &cleared);
  cleared.checksum ^= 1;
  assert(!validFirmwareJournalRecord(cleared));
  assert(newestValidFirmwareJournalRecord(&pending, &cleared) == &pending);
  pending.image_sha256[0] ^= 1;
  assert(!validFirmwareJournalRecord(pending));
  return 0;
}
