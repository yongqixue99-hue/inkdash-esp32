#pragma once

#include <GxEPD2_3C.h>
#include <epd3c/GxEPD2_750c_Z08.h>

#include "board_config.h"
#include "battery_monitor.h"
#include "dashboard_data.h"
#include "frame_asset.h"
#include "tri_color_canvas.h"
#include "wallpaper_cache.h"

namespace inkdash {

class DisplayController {
 public:
  DisplayController();

  void begin();
  bool showCodex(const FrameAsset& frame, const CodexDashboardData* data,
                 DataStatus status, const BatteryReading* battery,
                 const char* setup_ssid = nullptr);
  bool showServer(const FrameAsset& frame, const ServerDashboardData* data,
                  DataStatus status, const BatteryReading* battery);
  bool showWallpaperFromUrl(const char* endpoint,
                            WallpaperCache* cache = nullptr,
                            const char* page_id = "wallpaper",
                            const BatteryReading* battery = nullptr);

 private:
  bool loadFrame(const FrameAsset& frame);
  bool commit(const char* page_id);
  bool decodePlane(const RlePlane& source, uint8_t* destination,
                   size_t capacity) const;
  uint32_t crc32(const uint8_t* data, size_t size) const;
  bool showCachedWallpaper(WallpaperCache* cache, const char* page_id,
                           const BatteryReading* battery);
  void renderCodex(const CodexDashboardData* data, DataStatus status,
                   const BatteryReading* battery, const char* setup_ssid);
  void renderServer(const ServerDashboardData* data, DataStatus status,
                    const BatteryReading* battery);
  void drawArc(int16_t center_x, int16_t center_y, int16_t radius,
               int16_t thickness, uint8_t percent, uint16_t color);

  uint8_t black_plane_[board::kPlaneBytes]{};
  uint8_t red_plane_[board::kPlaneBytes]{};
  TriColorCanvas canvas_;
  GxEPD2_750c_Z08 panel_;
};

}  // namespace inkdash
