#pragma once

#include <stdint.h>

#ifndef INKDASH_FIRMWARE_HARDWARE_ID
#define INKDASH_FIRMWARE_HARDWARE_ID "inkdash-esp32c3-75-bwr-v1"
#endif

#ifndef INKDASH_FIRMWARE_VERSION_CODE
#define INKDASH_FIRMWARE_VERSION_CODE 2026082700
#endif

#ifndef INKDASH_FIRMWARE_VERSION
#define INKDASH_FIRMWARE_VERSION "1.0.0"
#endif

namespace inkdash::firmware {

constexpr char kHardwareId[] = INKDASH_FIRMWARE_HARDWARE_ID;
constexpr uint32_t kVersionCode = INKDASH_FIRMWARE_VERSION_CODE;
constexpr char kVersion[] = INKDASH_FIRMWARE_VERSION;
extern const char kReleaseMarker[];

}  // namespace inkdash::firmware
