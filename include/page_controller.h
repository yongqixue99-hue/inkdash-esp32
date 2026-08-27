#pragma once

#include <Arduino.h>

namespace inkdash {

enum class DashboardPage : uint8_t {
  kCodex,
  kServer,
  kWallpaper,
  kHealth,
};

class PageController {
 public:
  DashboardPage current() const;
  DashboardPage next();
  DashboardPage select(size_t page_index);
  size_t index() const;
  size_t count() const;

 private:
  size_t page_index_ = 0;
};

}  // namespace inkdash
