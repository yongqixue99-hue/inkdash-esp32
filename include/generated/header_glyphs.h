#pragma once

#include <Arduino.h>

namespace inkdash::header_glyphs {

constexpr int16_t kHeaderGlyphWidth = 24;
constexpr int16_t kHeaderGlyphHeight = 24;
constexpr size_t kHeaderGlyphBytes = 72;

enum GlyphIndex : uint8_t {
  kMonth = 0,
  kDay = 1,
  kWeek = 2,
  kPeriod = 3,
  kOne = 4,
  kTwo = 5,
  kThree = 6,
  kFour = 7,
  kFive = 8,
  kSix = 9,
  kYi = 10,
  kGlyphCount = 11,
};

extern const uint8_t kGlyphBitmaps[kGlyphCount][kHeaderGlyphBytes];

}  // namespace inkdash::header_glyphs
