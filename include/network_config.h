#pragma once

#if __has_include("secrets.h")
#include "secrets.h"
#endif

#ifndef INKDASH_WIFI_SSID
#define INKDASH_WIFI_SSID ""
#endif

#ifndef INKDASH_WIFI_PASSWORD
#define INKDASH_WIFI_PASSWORD ""
#endif

#ifndef INKDASH_API_ORIGIN
#define INKDASH_API_ORIGIN "http://192.168.1.100:8767"
#endif

#ifndef INKDASH_CODEX_ENDPOINT
#define INKDASH_CODEX_ENDPOINT INKDASH_API_ORIGIN "/dashboard"
#endif

#ifndef INKDASH_SERVER_ENDPOINT
#define INKDASH_SERVER_ENDPOINT INKDASH_API_ORIGIN "/server-dashboard"
#endif

#ifndef INKDASH_CODEX_FALLBACK_ENDPOINT
#define INKDASH_CODEX_FALLBACK_ENDPOINT ""
#endif

#ifndef INKDASH_SERVER_FALLBACK_ENDPOINT
#define INKDASH_SERVER_FALLBACK_ENDPOINT ""
#endif

#ifndef INKDASH_WALLPAPER_ENDPOINT
#define INKDASH_WALLPAPER_ENDPOINT INKDASH_API_ORIGIN "/wallpaper.bin"
#endif

#ifndef INKDASH_HEALTH_ENDPOINT
#define INKDASH_HEALTH_ENDPOINT INKDASH_API_ORIGIN "/health.bin"
#endif

#ifndef INKDASH_FIRMWARE_CHANNEL
#define INKDASH_FIRMWARE_CHANNEL "stable"
#endif

#ifndef INKDASH_FIRMWARE_MANIFEST_ENDPOINT
#define INKDASH_FIRMWARE_MANIFEST_ENDPOINT ""
#endif

#ifndef INKDASH_FIRMWARE_URL_PREFIX
#define INKDASH_FIRMWARE_URL_PREFIX ""
#endif

namespace inkdash::network {

constexpr char kWifiSsid[] = INKDASH_WIFI_SSID;
constexpr char kWifiPassword[] = INKDASH_WIFI_PASSWORD;
constexpr char kCodexEndpoint[] = INKDASH_CODEX_ENDPOINT;
constexpr char kServerEndpoint[] = INKDASH_SERVER_ENDPOINT;
constexpr char kCodexFallbackEndpoint[] = INKDASH_CODEX_FALLBACK_ENDPOINT;
constexpr char kServerFallbackEndpoint[] = INKDASH_SERVER_FALLBACK_ENDPOINT;
constexpr char kWallpaperEndpoint[] = INKDASH_WALLPAPER_ENDPOINT;
constexpr char kHealthEndpoint[] = INKDASH_HEALTH_ENDPOINT;
constexpr char kFirmwareChannel[] = INKDASH_FIRMWARE_CHANNEL;
constexpr char kFirmwareManifestEndpoint[] =
    INKDASH_FIRMWARE_MANIFEST_ENDPOINT;
constexpr char kFirmwareUrlPrefix[] = INKDASH_FIRMWARE_URL_PREFIX;
constexpr char kProvisioningPassword[] = "inkdash75";
constexpr uint32_t kConnectTimeoutMs = 30000;
constexpr uint32_t kSavedWifiRetryIntervalMs = 30000;
// A short FTTR reboot must not replace the cached dashboard with a setup
// screen. A genuinely obsolete SSID/password still needs a case-closed escape
// hatch, so an hour of continuous association failure opens the protected AP.
constexpr uint32_t kSavedWifiRecoveryPortalDelayMs =
    60UL * 60UL * 1000UL;
constexpr uint32_t kRequestTimeoutMs = 6000;
constexpr uint32_t kWallpaperRequestTimeoutMs = 15000;
constexpr uint32_t kFirmwareRequestTimeoutMs = 10000;
constexpr uint32_t kFirmwareDownloadTimeoutMs = 30000;
constexpr uint8_t kPrimaryRequestAttempts = 2;
constexpr uint32_t kRequestRetryDelayMs = 750;
constexpr size_t kMaximumResponseBytes = 8192;
constexpr uint32_t kAutomaticRefreshMs = 4UL * 60UL * 60UL * 1000UL;
constexpr uint32_t kCodexPollIntervalMs = 30UL * 60UL * 1000UL;
constexpr uint32_t kPageFetchMaxAgeMs = 2UL * 60UL * 1000UL;
constexpr uint32_t kCodexSnapshotFreshAgeSeconds = 24UL * 60UL * 60UL;
constexpr uint32_t kServerSnapshotFreshAgeSeconds = 24UL * 60UL * 60UL;

}  // namespace inkdash::network
