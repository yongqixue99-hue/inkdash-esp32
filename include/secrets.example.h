#pragma once

// Optional: copy this file to include/secrets.h to compile local Wi-Fi
// credentials into a private build. Without this file the firmware first
// reads its guarded reserved-sector Wi-Fi record, then opens the protected
// InkDash-Setup-* captive portal. Never put ChatGPT/OpenAI credentials,
// account cookies, SSH keys, or server passwords on the ESP32.
#define INKDASH_WIFI_SSID "YOUR_WIFI_NAME"
#define INKDASH_WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

// Change this to the LAN address of host/dashboard_server.py. The ESP32 only
// receives validated numeric JSON and prepared image packages.
#define INKDASH_API_ORIGIN "http://192.168.1.100:8767"

// Optional second data source. Leave empty for the direct-LAN tutorial path.
// #define INKDASH_CODEX_FALLBACK_ENDPOINT ""
// #define INKDASH_SERVER_FALLBACK_ENDPOINT ""

// OTA is disabled by default. After generating your own signing key and
// publishing an HTTPS/HTTP release directory, configure both values together.
// #define INKDASH_FIRMWARE_MANIFEST_ENDPOINT "https://example.invalid/firmware/stable/manifest.json"
// #define INKDASH_FIRMWARE_URL_PREFIX "https://example.invalid/firmware/stable/"
