#include "tri_color_canvas.h"

#include "board_config.h"

namespace inkdash {

TriColorCanvas::TriColorCanvas(uint16_t width, uint16_t height,
                               uint8_t* black_plane, uint8_t* red_plane)
    : Adafruit_GFX(width, height),
      black_plane_(black_plane),
      red_plane_(red_plane) {
  setTextWrap(false);
}

void TriColorCanvas::drawPixel(int16_t x, int16_t y, uint16_t color) {
  if (x < 0 || y < 0 || x >= width() || y >= height()) {
    return;
  }
  const size_t byte_index =
      static_cast<size_t>(y) * (board::kDisplayWidth / 8) + x / 8;
  const uint8_t bit = 0x80 >> (x & 7);

  if (color == kInkBlack) {
    black_plane_[byte_index] &= ~bit;
    red_plane_[byte_index] |= bit;
  } else if (color == kInkRed) {
    black_plane_[byte_index] |= bit;
    red_plane_[byte_index] &= ~bit;
  } else {
    black_plane_[byte_index] |= bit;
    red_plane_[byte_index] |= bit;
  }
}

void TriColorCanvas::drawText(int16_t x, int16_t baseline, const String& text,
                              const GFXfont* font, uint8_t size,
                              uint16_t color, TextAlign align) {
  setFont(font);
  setTextSize(size);
  setTextColor(color);
  int16_t x1 = 0;
  int16_t y1 = 0;
  uint16_t width = 0;
  uint16_t height = 0;
  getTextBounds(text.c_str(), 0, baseline, &x1, &y1, &width, &height);
  int16_t cursor_x = x - x1;
  if (align == TextAlign::kCenter) {
    cursor_x -= width / 2;
  } else if (align == TextAlign::kRight) {
    cursor_x -= width;
  }
  setCursor(cursor_x, baseline);
  print(text);
}

}  // namespace inkdash
