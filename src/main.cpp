#include <Arduino.h>

#include "battery_monitor.h"
#include "board_config.h"
#include "button_input.h"
#include "codex_display_change.h"
#include "dashboard_client.h"
#include "dashboard_snapshot_store.h"
#include "display_controller.h"
#include "firmware_update_manager.h"
#include "flash_layout.h"
#include "generated/frame_assets.h"
#include "network_config.h"
#include "page_controller.h"
#include "page_state_store.h"
#include "wallpaper_cache.h"

namespace {

template <typename T>
struct DashboardState {
  T data{};
  bool has_data = false;
  inkdash::DataStatus status = inkdash::DataStatus::kOffline;
  uint32_t last_attempt_ms = 0;
  uint32_t last_success_ms = 0;
};

inkdash::DisplayController display;
inkdash::BatteryMonitor battery;
inkdash::DashboardClient dashboard_client;
inkdash::DashboardSnapshotStore snapshot_store;
inkdash::ButtonInput primary_page_touch(
    inkdash::board::kPrimaryPageTouchPin,
    inkdash::board::kPageTouchActiveLow,
    inkdash::board::kPageTouchUseInternalPullup,
    inkdash::board::kButtonDebounceMs);
inkdash::ButtonInput secondary_page_touch(
    inkdash::board::kSecondaryPageTouchPin,
    inkdash::board::kPageTouchActiveLow,
    inkdash::board::kPageTouchUseInternalPullup,
    inkdash::board::kButtonDebounceMs);
inkdash::PageController pages;
inkdash::PageStateStore page_state_store;
inkdash::WallpaperCache wallpaper_cache;
inkdash::WallpaperCache health_cache(inkdash::flash::kHealthSlotA,
                                     inkdash::flash::kHealthSlotB,
                                     "Health dashboard");
inkdash::FirmwareUpdateManager firmware_updater;
DashboardState<inkdash::CodexDashboardData> codex_state;
DashboardState<inkdash::ServerDashboardData> server_state;
uint32_t last_display_ms = 0;
uint32_t last_codex_poll_ms = 0;

bool expired(uint32_t now, uint32_t then, uint32_t maximum_age) {
  return then == 0 || now - then >= maximum_age;
}

void refreshCodex(bool force) {
  const uint32_t now = millis();
  if (!force && codex_state.has_data &&
      !expired(now, codex_state.last_success_ms,
               inkdash::network::kPageFetchMaxAgeMs)) {
    return;
  }
  codex_state.last_attempt_ms = now;
  inkdash::CodexDashboardData next;
  if (dashboard_client.fetchCodex(next)) {
    codex_state.data = next;
    codex_state.has_data = true;
    codex_state.status = dashboard_client.lastPayloadStale()
                             ? inkdash::DataStatus::kStale
                             : inkdash::DataStatus::kLive;
    codex_state.last_success_ms = millis();
    if (!snapshot_store.saveCodex(next)) {
      Serial.println("Codex snapshot was not persisted");
    }
  } else {
    codex_state.status = codex_state.has_data ? inkdash::DataStatus::kStale
                                             : inkdash::DataStatus::kOffline;
  }
}

void refreshServer(bool force) {
  const uint32_t now = millis();
  if (!force && server_state.has_data &&
      !expired(now, server_state.last_success_ms,
               inkdash::network::kPageFetchMaxAgeMs)) {
    return;
  }
  server_state.last_attempt_ms = now;
  inkdash::ServerDashboardData next;
  if (dashboard_client.fetchServer(next)) {
    server_state.data = next;
    server_state.has_data = true;
    server_state.status = dashboard_client.lastPayloadStale()
                              ? inkdash::DataStatus::kStale
                              : inkdash::DataStatus::kLive;
    server_state.last_success_ms = millis();
    if (!snapshot_store.saveServer(next)) {
      Serial.println("Server snapshot was not persisted");
    }
  } else {
    server_state.status = server_state.has_data ? inkdash::DataStatus::kStale
                                               : inkdash::DataStatus::kOffline;
  }
}

bool showCurrentPage(bool force_fetch) {
  battery.refreshIfDue(millis());
  Serial.printf("Page %u/%u\n", static_cast<unsigned>(pages.index() + 1),
                static_cast<unsigned>(pages.count()));
  bool displayed = false;
  if (pages.current() == inkdash::DashboardPage::kCodex) {
    refreshCodex(force_fetch);
    last_codex_poll_ms = millis();
    displayed = display.showCodex(
        inkdash::assets::kCodexQuota,
        codex_state.has_data ? &codex_state.data : nullptr, codex_state.status,
        &battery.reading(),
        dashboard_client.provisioning()
            ? dashboard_client.provisioningSsid().c_str()
            : nullptr);
  } else if (pages.current() == inkdash::DashboardPage::kServer) {
    refreshServer(force_fetch);
    displayed = display.showServer(
        inkdash::assets::kServerStatus,
        server_state.has_data ? &server_state.data : nullptr,
        server_state.status, &battery.reading());
  } else if (pages.current() == inkdash::DashboardPage::kWallpaper) {
    displayed = display.showWallpaperFromUrl(
        inkdash::network::kWallpaperEndpoint, &wallpaper_cache);
  } else {
    displayed = display.showWallpaperFromUrl(
        inkdash::network::kHealthEndpoint, &health_cache,
        "health-dashboard", &battery.reading());
  }
  if (!displayed) {
    Serial.println("Display update failed; keeping previous e-paper image");
  }
  last_display_ms = millis();
  // A full tri-color refresh blocks for roughly 18 seconds. Re-sample the
  // touch strip afterwards so a held or released button is not replayed.
  primary_page_touch.synchronize();
  secondary_page_touch.synchronize();
  return displayed;
}

void pollCodexAndRefreshIfChanged() {
  const bool had_data = codex_state.has_data;
  const inkdash::CodexDashboardData displayed = codex_state.data;
  const inkdash::DataStatus displayed_status = codex_state.status;

  refreshCodex(true);
  last_codex_poll_ms = millis();
  const bool changed =
      had_data != codex_state.has_data ||
      (had_data && codex_state.has_data &&
       inkdash::codexDisplayChanged(displayed, displayed_status,
                                    codex_state.data, codex_state.status));
  if (!changed) {
    Serial.println("Codex poll complete: visible values unchanged");
    return;
  }
  Serial.println("Codex visible values changed; refreshing e-paper");
  showCurrentPage(false);
}

bool restoreCachedSnapshots() {
  if (!snapshot_store.begin()) {
    return false;
  }
  if (snapshot_store.loadCodex(codex_state.data)) {
    codex_state.has_data = true;
    codex_state.status = inkdash::DataStatus::kStale;
    Serial.println("Cached Codex dashboard ready for offline display");
  }
  if (snapshot_store.loadServer(server_state.data)) {
    server_state.has_data = true;
    server_state.status = inkdash::DataStatus::kStale;
    Serial.println("Cached server dashboard ready for offline display");
  }
  return true;
}

void advancePageAndRemember() {
  pages.next();
  if (!page_state_store.save(pages.index())) {
    Serial.println("Page selection was not persisted");
  }
}

void advancePageAndRefresh() {
  advancePageAndRemember();
  // A page change already requires one full e-paper update. Fetch the target
  // numeric page first so that the update contains the newest available
  // server snapshot.
  // snapshot. refreshCodex/refreshServer retain the verified flash snapshot
  // when the network request fails, so page switching still works offline.
  showCurrentPage(true);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  const uint32_t serial_wait_started = millis();
  while (!Serial && millis() - serial_wait_started < 1500) {
    delay(10);
  }
  Serial.println("\nESP32-C3 Ink Dashboard starting (real data only)");
  firmware_updater.begin();

  const inkdash::PageStartupState page_startup =
      page_state_store.begin(pages.count());
  pages.select(page_startup.page_index);
  Serial.printf(
      "Boot reset reason=%d page_record=%s case_button=%s initial_page=%u write=%s\n",
      page_startup.reset_reason,
      page_startup.record_was_valid ? "valid" : "new",
      page_startup.case_button_restart ? "next-page" : "no",
      static_cast<unsigned>(pages.index() + 1),
      page_startup.write_succeeded ? "ok" : "failed");

  Serial.println("Initializing direct page touch inputs (GPIO9 + GPIO3)");
  primary_page_touch.begin();
  secondary_page_touch.begin();
  Serial.println("Initializing battery monitor (GPIO20 gate + GPIO0 ADC)");
  battery.begin();
  battery.refreshIfDue(millis(), true);
  Serial.println("Initializing 7.5-inch tri-color display");
  display.begin();
  Serial.println("Display initialized");
  Serial.println("Initializing persistent dashboard snapshots");
  const bool snapshot_storage_ready = restoreCachedSnapshots();
  Serial.println("Initializing persistent wallpaper cache");
  const bool wallpaper_cache_ready = wallpaper_cache.begin();
  Serial.println("Initializing persistent health dashboard cache");
  const bool health_cache_ready = health_cache.begin();
  Serial.println("Initializing Wi-Fi and dashboard client");
  dashboard_client.begin();
  Serial.println("Rendering initial page");
  const bool initial_page_ready = showCurrentPage(true);
  // Do not accept a newly booted OTA image until every persistent store has
  // been opened, the legacy Wi-Fi record has migrated out of the application
  // slot, and one complete e-paper render has succeeded.
  firmware_updater.confirmHealthy(snapshot_storage_ready &&
                                  wallpaper_cache_ready &&
                                  health_cache_ready &&
                                  dashboard_client.wifiStorageReady() &&
                                  initial_page_ready);
  Serial.println("Initial page render complete");
}

void loop() {
  if (dashboard_client.process()) {
    showCurrentPage(true);
  } else if (dashboard_client.consumeNextPageRequest()) {
    advancePageAndRefresh();
  } else if (dashboard_client.consumeBatterySampleRequest()) {
    battery.refreshIfDue(millis(), true);
    showCurrentPage(false);
  } else if (dashboard_client.consumeFirmwareUpdateRequest()) {
    firmware_updater.requestCheckNow();
  } else if (primary_page_touch.pollPressed()) {
    Serial.println("Page touch pressed: GPIO9 (KEY3)");
    advancePageAndRefresh();
  } else if (secondary_page_touch.pollPressed()) {
    Serial.println("Page touch pressed: GPIO3 (KEY1)");
    advancePageAndRefresh();
  } else if (pages.current() == inkdash::DashboardPage::kCodex &&
             expired(millis(), last_codex_poll_ms,
                     inkdash::network::kCodexPollIntervalMs)) {
    pollCodexAndRefreshIfChanged();
  } else if (expired(millis(), last_display_ms,
                     inkdash::network::kAutomaticRefreshMs)) {
    showCurrentPage(true);
  }
  firmware_updater.process(dashboard_client.connected(), battery.reading(),
                           millis());
  delay(5);
}
