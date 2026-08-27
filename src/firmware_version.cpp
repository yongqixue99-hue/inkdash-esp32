#include "firmware_version.h"

#define INKDASH_STRINGIFY_DETAIL(value) #value
#define INKDASH_STRINGIFY(value) INKDASH_STRINGIFY_DETAIL(value)

namespace inkdash::firmware {

// Release tooling scans this exact linked marker before it signs a binary.
// FirmwareUpdateManager also references it at boot, preventing linker garbage
// collection from removing an otherwise metadata-only constant.
const char kReleaseMarker[] =
    "INKDASH_RELEASE_V1|" INKDASH_FIRMWARE_HARDWARE_ID "|"
    INKDASH_STRINGIFY(INKDASH_FIRMWARE_VERSION_CODE) "|"
    INKDASH_FIRMWARE_VERSION;

}  // namespace inkdash::firmware
