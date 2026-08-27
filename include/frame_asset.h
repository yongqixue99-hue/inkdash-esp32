#pragma once

#include <stddef.h>
#include <stdint.h>

namespace inkdash {

struct RlePlane {
  const uint8_t* data;
  size_t compressed_size;
  size_t decoded_size;
  uint32_t crc32;
};

struct FrameAsset {
  const char* id;
  uint16_t width;
  uint16_t height;
  RlePlane black;
  RlePlane red;
};

}  // namespace inkdash
