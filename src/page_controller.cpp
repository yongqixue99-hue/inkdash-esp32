#include "page_controller.h"

namespace inkdash {

DashboardPage PageController::current() const {
  switch (page_index_) {
    case 0:
      return DashboardPage::kCodex;
    case 1:
      return DashboardPage::kServer;
    case 2:
      return DashboardPage::kWallpaper;
    default:
      return DashboardPage::kHealth;
  }
}

DashboardPage PageController::next() {
  page_index_ = (page_index_ + 1) % count();
  return current();
}

DashboardPage PageController::select(size_t page_index) {
  page_index_ = page_index % count();
  return current();
}

size_t PageController::index() const { return page_index_; }

size_t PageController::count() const { return 4; }

}  // namespace inkdash
