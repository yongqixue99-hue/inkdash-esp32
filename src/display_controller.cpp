#include "display_controller.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <SPI.h>
#include <math.h>
#include <pgmspace.h>
#include <string.h>
#include <time.h>
#include <WiFi.h>

#include "generated/header_glyphs.h"
#include "network_config.h"
#include "token_bucket_date.h"
#include "wallpaper_format.h"

namespace inkdash {
namespace {

constexpr uint32_t kHongKongOffsetSeconds = 8UL * 60UL * 60UL;
constexpr uint64_t kTokensPerYi = 100000000ULL;
constexpr float kPi = 3.14159265358979323846f;
constexpr int16_t kHeaderTextRight = 688;
constexpr int16_t kHeaderBatteryX = 704;
constexpr int16_t kHeaderBatteryY = 23;
constexpr int16_t kBatteryBodyWidth = 70;
constexpr int16_t kBatteryBodyHeight = 28;
constexpr int16_t kBatteryBorderWidth = 3;
constexpr int16_t kBatteryTerminalWidth = 6;
constexpr int16_t kQuotaCenterX = 224;
constexpr int16_t kQuotaLeft = 32;
constexpr int16_t kQuotaRight = 416;
constexpr int16_t kQuotaGap = 12;

bool readExact(WiFiClient& stream, uint8_t* destination, size_t size,
               uint32_t timeout_ms) {
  size_t received = 0;
  uint32_t last_progress_ms = millis();
  while (received < size) {
    const int available = stream.available();
    if (available > 0) {
      const size_t remaining = size - received;
      const size_t requested =
          static_cast<size_t>(available) < remaining
              ? static_cast<size_t>(available)
              : remaining;
      const int count = stream.read(destination + received, requested);
      if (count > 0) {
        received += static_cast<size_t>(count);
        last_progress_ms = millis();
        continue;
      }
    }
    if (!stream.connected() || millis() - last_progress_ms >= timeout_ms) {
      return false;
    }
    delay(2);
  }
  return true;
}

const char* statusText(DataStatus status) {
  switch (status) {
    case DataStatus::kLive:
      return "LIVE";
    case DataStatus::kStale:
      return "STALE";
    case DataStatus::kOffline:
    default:
      return "OFFLINE";
  }
}

uint16_t statusColor(DataStatus status) {
  return status == DataStatus::kLive ? kInkBlack : kInkRed;
}

bool localTime(uint32_t epoch, tm& output) {
  if (epoch == 0) {
    return false;
  }
  const time_t adjusted = static_cast<time_t>(epoch + kHongKongOffsetSeconds);
  return gmtime_r(&adjusted, &output) != nullptr;
}

String twoDigits(int value) {
  char buffer[4];
  snprintf(buffer, sizeof(buffer), "%02d", value);
  return String(buffer);
}

String tokenYi(uint64_t tokens) {
  return String(static_cast<double>(tokens) / kTokensPerYi, 2);
}

String tokenBucketDateLabel(const TokenBucketDate& bucket) {
  if (!bucket.valid) {
    return "--.--";
  }
  char buffer[8];
  snprintf(buffer, sizeof(buffer), "%02u.%02u",
           static_cast<unsigned>(bucket.month),
           static_cast<unsigned>(bucket.day));
  return String(buffer);
}

uint16_t measureTextWidth(TriColorCanvas& canvas, const String& text,
                          const GFXfont* font, uint8_t size) {
  canvas.setFont(font);
  canvas.setTextSize(size);
  int16_t x1 = 0;
  int16_t y1 = 0;
  uint16_t width = 0;
  uint16_t height = 0;
  canvas.getTextBounds(text.c_str(), 0, 0, &x1, &y1, &width, &height);
  return width;
}

void drawHeaderGlyph(TriColorCanvas& canvas, int16_t x, int16_t y,
                     header_glyphs::GlyphIndex glyph, uint16_t color);

void drawTokenValue(TriColorCanvas& canvas, int16_t center_x,
                    int16_t baseline_y, bool available, uint64_t tokens,
                    uint16_t color) {
  const String text = available ? tokenYi(tokens) : String("--");
  constexpr uint16_t kUnitGap = 2;
  constexpr uint16_t kUnitWidth = header_glyphs::kHeaderGlyphWidth;
  const GFXfont* font = &FreeSansBold24pt7b;
  uint16_t text_width = measureTextWidth(canvas, text, font, 1);
  if (text_width + kUnitGap + kUnitWidth > 120) {
    font = &FreeSansBold18pt7b;
    text_width = measureTextWidth(canvas, text, font, 1);
  }
  if (text_width + kUnitGap + kUnitWidth > 120) {
    font = &FreeSansBold12pt7b;
    text_width = measureTextWidth(canvas, text, font, 1);
  }
  const int16_t group_width =
      static_cast<int16_t>(text_width + kUnitGap + kUnitWidth);
  const int16_t text_x = center_x - group_width / 2;
  canvas.drawText(text_x, baseline_y, text, font, 1, color);
  drawHeaderGlyph(canvas, text_x + text_width + kUnitGap, baseline_y - 23,
                  header_glyphs::kYi, color);
}

String trafficGb(uint32_t centi_gb) {
  if (centi_gb >= 100000) {
    return String(static_cast<double>(centi_gb) / 100000.0, 2) + "T";
  }
  return String(static_cast<double>(centi_gb) / 100.0, 1) + "G";
}

String trafficQuota(uint32_t centi_gb) {
  if (centi_gb >= 100000) {
    return String(static_cast<double>(centi_gb) / 100000.0, 2) + " TB";
  }
  return String(static_cast<double>(centi_gb) / 100.0, 1) + " GB";
}

bool splitIsoDate(const char* source, int& year, int& month, int& day) {
  if (source == nullptr || strlen(source) != 10 || source[4] != '-' ||
      source[7] != '-') {
    return false;
  }
  return sscanf(source, "%d-%d-%d", &year, &month, &day) == 3 &&
         year >= 2000 && year <= 9999 && month >= 1 && month <= 12 &&
         day >= 1 && day <= 31;
}

String dotDate(const char* source) {
  int year = 0;
  int month = 0;
  int day = 0;
  if (!splitIsoDate(source, year, month, day)) {
    return "--";
  }
  char buffer[12];
  snprintf(buffer, sizeof(buffer), "%04d.%02d.%02d", year, month, day);
  return String(buffer);
}

void drawHeaderGlyph(TriColorCanvas& canvas, int16_t x, int16_t y,
                     header_glyphs::GlyphIndex glyph, uint16_t color) {
  canvas.drawBitmap(x, y, header_glyphs::kGlyphBitmaps[glyph],
                    header_glyphs::kHeaderGlyphWidth,
                    header_glyphs::kHeaderGlyphHeight, color);
}

void drawHeaderDate(TriColorCanvas& canvas, uint32_t epoch, uint16_t color) {
  tm value{};
  if (!localTime(epoch, value)) {
    return;
  }
  static const header_glyphs::GlyphIndex kWeekdayFinal[] = {
      header_glyphs::kDay,   header_glyphs::kOne,
      header_glyphs::kTwo,   header_glyphs::kThree,
      header_glyphs::kFour,  header_glyphs::kFive,
      header_glyphs::kSix,
  };

  canvas.drawText(512, 49, String(value.tm_mon + 1),
                  &FreeSansBold18pt7b, 1, color, TextAlign::kRight);
  drawHeaderGlyph(canvas, 516, 24, header_glyphs::kMonth, color);
  canvas.drawText(578, 49, String(value.tm_mday), &FreeSansBold18pt7b, 1,
                  color, TextAlign::kRight);
  drawHeaderGlyph(canvas, 582, 24, header_glyphs::kDay, color);
  drawHeaderGlyph(canvas, 616, 24, header_glyphs::kWeek, color);
  drawHeaderGlyph(canvas, 640, 24, header_glyphs::kPeriod, color);
  drawHeaderGlyph(canvas, 664, 24, kWeekdayFinal[value.tm_wday], color);
}

void drawBattery(TriColorCanvas& canvas, const BatteryReading* battery,
                 uint16_t color) {
  // The frame is always visible: black outline, white empty capacity and a
  // proportional black fill. This prevents an invalid ADC sample from making
  // the entire battery disappear.
  canvas.fillRect(kHeaderBatteryX - 2, kHeaderBatteryY - 4, 98, 48,
                  kInkWhite);
  canvas.fillRect(kHeaderBatteryX, kHeaderBatteryY, kBatteryBodyWidth,
                  kBatteryBodyHeight, color);
  canvas.fillRect(kHeaderBatteryX + kBatteryBorderWidth,
                  kHeaderBatteryY + kBatteryBorderWidth,
                  kBatteryBodyWidth - 2 * kBatteryBorderWidth,
                  kBatteryBodyHeight - 2 * kBatteryBorderWidth, kInkWhite);
  canvas.fillRect(kHeaderBatteryX + kBatteryBodyWidth,
                  kHeaderBatteryY + 8, kBatteryTerminalWidth,
                  kBatteryBodyHeight - 16, color);

  String label = "?";
  int16_t fill_width = 0;
  if (battery != nullptr && battery->valid) {
    const int16_t inner_width =
        kBatteryBodyWidth - 2 * kBatteryBorderWidth;
    fill_width = static_cast<int16_t>(
        (static_cast<uint32_t>(inner_width) * battery->percent + 50U) / 100U);
    label = String(battery->percent) + "%";
  } else if (battery != nullptr && battery->external_power) {
    // External Type-C power makes the divided ADC reading unsuitable for a
    // battery percentage. Show a charging mark without pretending it is 100%.
    label = "";
  }
  if (fill_width > 0) {
    canvas.fillRect(kHeaderBatteryX + kBatteryBorderWidth,
                    kHeaderBatteryY + kBatteryBorderWidth, fill_width,
                    kBatteryBodyHeight - 2 * kBatteryBorderWidth, color);
  }
  if (battery != nullptr && battery->external_power && !battery->valid) {
    const int16_t center_x = kHeaderBatteryX + kBatteryBodyWidth / 2;
    canvas.fillTriangle(center_x + 3, kHeaderBatteryY + 4, center_x - 6,
                        kHeaderBatteryY + 15, center_x + 1,
                        kHeaderBatteryY + 15, kInkRed);
    canvas.fillTriangle(center_x, kHeaderBatteryY + 12, center_x + 7,
                        kHeaderBatteryY + 12, center_x - 4,
                        kHeaderBatteryY + 24, kInkRed);
  }
  if (label.length() > 0) {
    canvas.drawText(kHeaderBatteryX + kBatteryBodyWidth / 2, 67, label,
                    &FreeSansBold9pt7b, 1, color, TextAlign::kCenter);
  }
}

}  // namespace

DisplayController::DisplayController()
    : canvas_(board::kDisplayWidth, board::kDisplayHeight, black_plane_,
              red_plane_),
      panel_(board::kEpdChipSelectPin, board::kEpdDataCommandPin,
             board::kEpdResetPin, board::kEpdBusyPin) {}

void DisplayController::begin() {
  SPI.begin(board::kEpdClockPin, -1, board::kEpdMosiPin,
            board::kEpdChipSelectPin);
  panel_.init(115200, true, 10, false);
}

bool DisplayController::showCodex(const FrameAsset& frame,
                                  const CodexDashboardData* data,
                                  DataStatus status,
                                  const BatteryReading* battery,
                                  const char* setup_ssid) {
  if (!loadFrame(frame)) {
    return false;
  }
  renderCodex(data, status, battery, setup_ssid);
  return commit(frame.id);
}

bool DisplayController::showServer(const FrameAsset& frame,
                                   const ServerDashboardData* data,
                                   DataStatus status,
                                   const BatteryReading* battery) {
  if (!loadFrame(frame)) {
    return false;
  }
  renderServer(data, status, battery);
  return commit(frame.id);
}

bool DisplayController::showWallpaperFromUrl(const char* endpoint,
                                             WallpaperCache* cache,
                                             const char* page_id,
                                             const BatteryReading* battery) {
  if (endpoint == nullptr || endpoint[0] == '\0' ||
      WiFi.status() != WL_CONNECTED) {
    Serial.printf("%s download unavailable: Wi-Fi or endpoint missing\n",
                  page_id);
    return showCachedWallpaper(cache, page_id, battery);
  }

  WiFiClient transport;
  HTTPClient request;
  request.setConnectTimeout(network::kWallpaperRequestTimeoutMs);
  request.setTimeout(network::kWallpaperRequestTimeoutMs);
  if (!request.begin(transport, endpoint)) {
    Serial.printf("%s download failed: HTTP client initialization\n",
                  page_id);
    return showCachedWallpaper(cache, page_id, battery);
  }
  request.addHeader("Accept", "application/vnd.inkdash.wallpaper");
  request.setUserAgent("InkDash-ESP32C3/1");
  const int status = request.GET();
  if (status != HTTP_CODE_OK) {
    Serial.printf("%s download failed: HTTP status %d\n", page_id, status);
    request.end();
    return showCachedWallpaper(cache, page_id, battery);
  }
  const size_t expected_size =
      kWallpaperHeaderBytes + 2 * board::kPlaneBytes;
  if (request.getSize() != static_cast<int>(expected_size)) {
    Serial.printf("%s download failed: expected %u bytes, got %d\n", page_id,
                  static_cast<unsigned>(expected_size), request.getSize());
    request.end();
    return showCachedWallpaper(cache, page_id, battery);
  }

  WiFiClient* stream = request.getStreamPtr();
  uint8_t header_bytes[kWallpaperHeaderBytes]{};
  WallpaperHeader header;
  const bool header_read =
      stream != nullptr &&
      readExact(*stream, header_bytes, sizeof(header_bytes),
                network::kWallpaperRequestTimeoutMs);
  if (!header_read ||
      !parseWallpaperHeader(header_bytes, sizeof(header_bytes),
                            board::kDisplayWidth, board::kDisplayHeight,
                            board::kPlaneBytes, header)) {
    Serial.printf("%s download failed: invalid header\n", page_id);
    request.end();
    return showCachedWallpaper(cache, page_id, battery);
  }
  const bool planes_read =
      readExact(*stream, black_plane_, sizeof(black_plane_),
                network::kWallpaperRequestTimeoutMs) &&
      readExact(*stream, red_plane_, sizeof(red_plane_),
                network::kWallpaperRequestTimeoutMs);
  request.end();
  if (!planes_read) {
    Serial.printf("%s download failed: truncated color planes\n", page_id);
    return showCachedWallpaper(cache, page_id, battery);
  }
  const uint32_t black_crc =
      wallpaperCrc32(black_plane_, sizeof(black_plane_));
  const uint32_t red_crc = wallpaperCrc32(red_plane_, sizeof(red_plane_));
  if (black_crc != header.black_crc32 || red_crc != header.red_crc32) {
    Serial.printf(
        "%s download failed: CRC black=%08lx/%08lx red=%08lx/%08lx\n",
        page_id,
        static_cast<unsigned long>(black_crc),
        static_cast<unsigned long>(header.black_crc32),
        static_cast<unsigned long>(red_crc),
        static_cast<unsigned long>(header.red_crc32));
    return showCachedWallpaper(cache, page_id, battery);
  }
  Serial.printf("%s payload validated: %u bytes from data service\n", page_id,
                static_cast<unsigned>(expected_size));
  if (cache != nullptr &&
      !cache->save(header_bytes, sizeof(header_bytes), black_plane_,
                   sizeof(black_plane_), red_plane_, sizeof(red_plane_))) {
    Serial.printf("%s was displayed but could not be cached\n", page_id);
  }
  if (battery != nullptr) {
    drawBattery(canvas_, battery, kInkBlack);
  }
  return commit(page_id);
}

bool DisplayController::showCachedWallpaper(
    WallpaperCache* cache, const char* page_id,
    const BatteryReading* battery) {
  if (cache == nullptr ||
      !cache->load(black_plane_, sizeof(black_plane_), red_plane_,
                   sizeof(red_plane_))) {
    return false;
  }
  if (battery != nullptr) {
    drawBattery(canvas_, battery, kInkBlack);
  }
  String cached_id = String("cached-") + page_id;
  return commit(cached_id.c_str());
}

bool DisplayController::loadFrame(const FrameAsset& frame) {
  if (frame.width != board::kDisplayWidth ||
      frame.height != board::kDisplayHeight) {
    Serial.printf("Frame %s has invalid dimensions: %ux%u\n", frame.id,
                  frame.width, frame.height);
    return false;
  }

  if (!decodePlane(frame.black, black_plane_, sizeof(black_plane_)) ||
      !decodePlane(frame.red, red_plane_, sizeof(red_plane_))) {
    Serial.printf("Frame %s failed RLE or CRC validation\n", frame.id);
    return false;
  }
  return true;
}

bool DisplayController::commit(const char* page_id) {
  Serial.printf("Displaying live page: %s\n", page_id);
  panel_.drawImage(black_plane_, red_plane_, 0, 0, board::kDisplayWidth,
                   board::kDisplayHeight, false, false, false);
  panel_.hibernate();
  Serial.println("Display refresh complete");
  return true;
}

void DisplayController::renderCodex(const CodexDashboardData* data,
                                    DataStatus status,
                                    const BatteryReading* battery,
                                    const char* setup_ssid) {
  drawBattery(canvas_, battery, kInkBlack);
  if (data == nullptr) {
    if (setup_ssid != nullptr && setup_ssid[0] != '\0') {
      canvas_.drawText(224, 210, "WIFI SETUP", &FreeSansBold24pt7b, 1,
                       kInkRed, TextAlign::kCenter);
      canvas_.drawText(224, 250, setup_ssid, &FreeSansBold9pt7b, 1,
                       kInkBlack, TextAlign::kCenter);
      canvas_.drawText(224, 280, "PASS: inkdash75", &FreeSansBold9pt7b, 1,
                       kInkBlack, TextAlign::kCenter);
    } else {
      canvas_.drawText(224, 255, "NO DATA", &FreeSansBold24pt7b, 2,
                       kInkRed, TextAlign::kCenter);
    }
    canvas_.drawText(770, 464, statusText(status), &FreeSansBold9pt7b, 1,
                     statusColor(status), TextAlign::kRight);
    return;
  }

  drawHeaderDate(canvas_, data->generated_at, kInkBlack);

  const uint16_t quota_color =
      data->remaining_percent <= 20 ? kInkRed : kInkBlack;
  const String quota_text(data->remaining_percent);
  uint8_t quota_size = 4;
  uint16_t quota_width =
      measureTextWidth(canvas_, quota_text, &FreeSansBold24pt7b, quota_size);
  const uint16_t percent_width =
      measureTextWidth(canvas_, "%", &FreeSansBold24pt7b, 1);
  while (quota_size > 2 &&
         quota_width + kQuotaGap + percent_width >
             kQuotaRight - kQuotaLeft) {
    --quota_size;
    quota_width =
        measureTextWidth(canvas_, quota_text, &FreeSansBold24pt7b, quota_size);
  }
  const int16_t quota_group_width =
      static_cast<int16_t>(quota_width + kQuotaGap + percent_width);
  int16_t quota_x = kQuotaCenterX - quota_group_width / 2;
  if (quota_x < kQuotaLeft) {
    quota_x = kQuotaLeft;
  }
  canvas_.drawText(quota_x, 296, quota_text, &FreeSansBold24pt7b,
                   quota_size, quota_color, TextAlign::kLeft);
  canvas_.drawText(quota_x + quota_width + kQuotaGap, 276, "%",
                   &FreeSansBold24pt7b, 1, quota_color, TextAlign::kLeft);
  canvas_.drawText(416, 354, String(data->used_percent) + "%",
                   &FreeSansBold12pt7b, 1, kInkBlack, TextAlign::kRight);
  tm reset{};
  if (localTime(data->reset_at, reset)) {
    canvas_.drawText(32, 438, twoDigits(reset.tm_mon + 1),
                     &FreeSansBold24pt7b, 1, kInkBlack);
    canvas_.drawText(148, 438, twoDigits(reset.tm_mday),
                     &FreeSansBold24pt7b, 1, kInkRed);
    char reset_time[8];
    snprintf(reset_time, sizeof(reset_time), "%02d:%02d", reset.tm_hour,
             reset.tm_min);
    canvas_.drawText(416, 438, reset_time, &FreeSansBold24pt7b, 1, kInkBlack,
                     TextAlign::kRight);
  }

  tm generated{};
  TodayYesterdayTokenBuckets pair;
  if (localTime(data->generated_at, generated)) {
    pair = resolveTodayYesterdayTokenBuckets(
        static_cast<uint16_t>(generated.tm_year + 1900),
        static_cast<uint8_t>(generated.tm_mon + 1),
        static_cast<uint8_t>(generated.tm_mday), data->daily_day_of_month,
        data->daily_tokens, kUsageDayCount);
  }
  canvas_.drawText(566, 120, tokenBucketDateLabel(pair.yesterday_date),
                   &FreeSansBold9pt7b, 1, kInkBlack);
  canvas_.drawText(698, 120, tokenBucketDateLabel(pair.today_date),
                   &FreeSansBold9pt7b, 1, kInkRed);
  drawTokenValue(canvas_, 574, 181, pair.yesterday_available,
                 pair.yesterday_tokens, kInkBlack);
  drawTokenValue(canvas_, 706, 181, pair.today_available, pair.today_tokens,
                 kInkRed);
  drawTokenValue(canvas_, 574, 283, true, data->week_tokens, kInkBlack);
  drawTokenValue(canvas_, 706, 283, data->cumulative_tokens > 0,
                 data->cumulative_tokens, kInkBlack);

  uint16_t maximum = 1;
  for (size_t index = 0; index < kUsageDayCount; ++index) {
    maximum = max(maximum, data->daily_usage_centi_yi[index]);
  }
  constexpr int16_t kChartBottom = 405;
  constexpr int16_t kChartHeight = 64;
  constexpr int16_t kBarWidth = 21;
  constexpr int16_t kBarStep = 36;
  for (size_t index = 0; index < kUsageDayCount; ++index) {
    const int16_t x = 512 + index * kBarStep;
    const uint16_t value = data->daily_usage_centi_yi[index];
    const int16_t height =
        value == 0 ? 0 : max(3, value * kChartHeight / maximum);
    const uint16_t color =
        index == kUsageDayCount - 1 ? kInkRed : kInkBlack;
    if (height > 0) {
      canvas_.fillRect(x, kChartBottom - height, kBarWidth, height, color);
    }
    canvas_.drawText(x + kBarWidth / 2, 431,
                     twoDigits(data->daily_day_of_month[index]),
                     &FreeSansBold9pt7b, 1, color, TextAlign::kCenter);
  }

  canvas_.drawText(770, 464, statusText(status), &FreeSansBold9pt7b, 1,
                   statusColor(status), TextAlign::kRight);
}

void DisplayController::renderServer(const ServerDashboardData* data,
                                     DataStatus status,
                                     const BatteryReading* battery) {
  drawBattery(canvas_, battery, kInkBlack);
  if (data != nullptr && data->generated_at > 0) {
    drawHeaderDate(canvas_, data->generated_at, kInkBlack);
  }

  const bool configured = data != nullptr && data->configured;
  if (configured) {
    const uint8_t traffic_percent =
        data->traffic_limit_centi_gb == 0
            ? 0
            : min<uint32_t>(
                  100, (static_cast<uint64_t>(data->traffic_used_centi_gb) *
                        100 + data->traffic_limit_centi_gb / 2) /
                           data->traffic_limit_centi_gb);
    const uint16_t traffic_color =
        traffic_percent >= 85 ? kInkRed : kInkBlack;
    if (data->traffic_limit_centi_gb > 0) {
      drawArc(270, 207, 96, 12, traffic_percent, traffic_color);
    }
    const uint32_t traffic_remaining =
        data->traffic_limit_centi_gb > data->traffic_used_centi_gb
            ? data->traffic_limit_centi_gb - data->traffic_used_centi_gb
            : 0;
    canvas_.drawText(270, 225,
                     String(static_cast<double>(traffic_remaining) / 100.0,
                            1),
                     &FreeSansBold24pt7b, 1, traffic_color,
                     TextAlign::kCenter);
    canvas_.drawText(410, 198, trafficGb(data->traffic_used_centi_gb),
                     &FreeSansBold18pt7b, 1, kInkBlack);
    canvas_.drawText(410, 280,
                     data->traffic_limit_centi_gb > 0
                         ? trafficGb(data->traffic_limit_centi_gb)
                         : "--",
                     &FreeSansBold18pt7b, 1, kInkBlack);

    const uint8_t values[] = {data->cpu_percent, data->memory_percent,
                              data->disk_percent};
    const int16_t centers[] = {95, 270, 445};
    for (size_t index = 0; index < 3; ++index) {
      const uint16_t color = values[index] >= 90 ? kInkRed : kInkBlack;
      drawArc(centers[index], 405, 49, 8, values[index], color);
      canvas_.drawText(centers[index], 411, String(values[index]) + "%",
                       &FreeSansBold18pt7b, 1, color, TextAlign::kCenter);
    }

    int expiry_year = 0;
    int expiry_month = 0;
    int expiry_day = 0;
    if (splitIsoDate(data->expiry_date, expiry_year, expiry_month,
                     expiry_day)) {
      canvas_.drawText(758, 158, String(expiry_year), &FreeSansBold9pt7b, 1,
                       kInkBlack, TextAlign::kRight);
      char expiry_short[8];
      snprintf(expiry_short, sizeof(expiry_short), "%02d.%02d",
               expiry_month, expiry_day);
      canvas_.drawText(673, 218, expiry_short, &FreeSansBold24pt7b, 1,
                       kInkBlack, TextAlign::kCenter);
      canvas_.drawText(720, 326, String(data->expiry_days_remaining),
                       &FreeSansBold24pt7b, 1, kInkRed,
                       TextAlign::kRight);
    } else {
      canvas_.drawText(673, 218, "--", &FreeSansBold24pt7b, 1, kInkRed,
                       TextAlign::kCenter);
      canvas_.drawText(720, 326, "--", &FreeSansBold24pt7b, 1, kInkRed,
                       TextAlign::kRight);
    }
    canvas_.drawText(770, 379, dotDate(data->traffic_reset_date),
                     &FreeSansBold9pt7b, 1, kInkBlack, TextAlign::kRight);
    canvas_.drawText(
        770, 415,
        data->plan_limit_centi_gb > 0
            ? trafficQuota(data->plan_limit_centi_gb)
            : "--",
        &FreeSansBold9pt7b, 1, kInkBlack, TextAlign::kRight);
    if (status != DataStatus::kLive) {
      canvas_.drawText(770, 464, statusText(status), &FreeSansBold9pt7b, 1,
                       kInkRed, TextAlign::kRight);
    }
    return;
  }

  const bool setup = data != nullptr && !data->configured &&
                     status != DataStatus::kOffline;
  const char* label = setup ? "SETUP" : "OFFLINE";
  canvas_.drawText(270, 225, "--", &FreeSansBold24pt7b, 1, kInkRed,
                   TextAlign::kCenter);
  canvas_.drawText(410, 198, label, &FreeSansBold18pt7b, 1, kInkRed);
  canvas_.drawText(410, 280, "--", &FreeSansBold18pt7b, 1, kInkRed);
  const int16_t centers[] = {95, 270, 445};
  for (int16_t center : centers) {
    canvas_.drawText(center, 411, "--", &FreeSansBold18pt7b, 1, kInkRed,
                     TextAlign::kCenter);
  }
  canvas_.drawText(673, 218, "--", &FreeSansBold24pt7b, 1, kInkRed,
                   TextAlign::kCenter);
  canvas_.drawText(720, 326, "--", &FreeSansBold24pt7b, 1, kInkRed,
                   TextAlign::kRight);
  canvas_.drawText(770, 379, "--", &FreeSansBold9pt7b, 1, kInkRed,
                   TextAlign::kRight);
  canvas_.drawText(770, 415, "--", &FreeSansBold9pt7b, 1, kInkRed,
                   TextAlign::kRight);
}

void DisplayController::drawArc(int16_t center_x, int16_t center_y,
                                int16_t radius, int16_t thickness,
                                uint8_t percent, uint16_t color) {
  if (percent == 0) {
    return;
  }
  const int end_degree = min(360, static_cast<int>(percent) * 360 / 100);
  int16_t previous_x = center_x;
  int16_t previous_y = center_y - radius;
  const int16_t dot_radius = max<int16_t>(1, thickness / 2);
  for (int degree = 0; degree <= end_degree; degree += 2) {
    const float radians = (degree - 90) * kPi / 180.0f;
    const int16_t x = center_x + roundf(cosf(radians) * radius);
    const int16_t y = center_y + roundf(sinf(radians) * radius);
    canvas_.drawLine(previous_x, previous_y, x, y, color);
    canvas_.fillCircle(x, y, dot_radius, color);
    previous_x = x;
    previous_y = y;
  }
}

bool DisplayController::decodePlane(const RlePlane& source,
                                    uint8_t* destination,
                                    size_t capacity) const {
  if (source.decoded_size != capacity || source.compressed_size % 2 != 0) {
    return false;
  }

  size_t input = 0;
  size_t output = 0;
  while (input < source.compressed_size) {
    const uint8_t count = pgm_read_byte(source.data + input++);
    const uint8_t value = pgm_read_byte(source.data + input++);
    if (count == 0 || output + count > capacity) {
      return false;
    }
    memset(destination + output, value, count);
    output += count;
  }

  return output == capacity && crc32(destination, output) == source.crc32;
}

uint32_t DisplayController::crc32(const uint8_t* data, size_t size) const {
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < size; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      const uint32_t mask = -(crc & 1u);
      crc = (crc >> 1u) ^ (0xEDB88320u & mask);
    }
  }
  return ~crc;
}

}  // namespace inkdash
