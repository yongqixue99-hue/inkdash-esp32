#pragma once

#include <Adafruit_GFX.h>

namespace inkdash {

constexpr uint16_t kInkBlack = 0x0000;
constexpr uint16_t kInkRed = 0xF800;
constexpr uint16_t kInkWhite = 0xFFFF;

enum class TextAlign : uint8_t {
  kLeft,
  kCenter,
  kRight,
};

class TriColorCanvas : public Adafruit_GFX {
 public:
  TriColorCanvas(uint16_t width, uint16_t height, uint8_t* black_plane,
                 uint8_t* red_plane);

  void drawPixel(int16_t x, int16_t y, uint16_t color) override;
  void drawText(int16_t x, int16_t baseline, const String& text,
                const GFXfont* font, uint8_t size, uint16_t color,
                TextAlign align = TextAlign::kLeft);

 private:
  uint8_t* black_plane_;
  uint8_t* red_plane_;
};

}  // namespace inkdash
