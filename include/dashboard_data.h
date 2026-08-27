#pragma once

#include <stddef.h>
#include <stdint.h>

namespace inkdash {

constexpr size_t kUsageDayCount = 7;

enum class DataStatus : uint8_t {
  kLive,
  kStale,
  kOffline,
};

struct CodexDashboardData {
  uint8_t remaining_percent = 0;
  uint8_t used_percent = 0;
  char reset_date[11]{};
  uint32_t reset_at = 0;
  char usage_scope[12]{};
  uint8_t daily_day_of_month[kUsageDayCount]{};
  uint64_t daily_tokens[kUsageDayCount]{};
  uint16_t daily_usage_centi_yi[kUsageDayCount]{};
  uint64_t today_tokens = 0;
  uint64_t week_tokens = 0;
  uint64_t cumulative_tokens = 0;
  uint32_t generated_at = 0;
};

struct ServerDashboardData {
  char name[16]{};
  bool configured = false;
  uint32_t traffic_used_centi_gb = 0;
  uint32_t traffic_limit_centi_gb = 0;
  uint32_t plan_limit_centi_gb = 0;
  char traffic_period[9]{};
  char expiry_date[11]{};
  uint16_t expiry_days_remaining = 0;
  char traffic_reset_date[11]{};
  uint8_t cpu_percent = 0;
  uint8_t memory_percent = 0;
  uint8_t disk_percent = 0;
  uint32_t rx_centi_gb = 0;
  uint32_t tx_centi_gb = 0;
  uint16_t uptime_days = 0;
  uint32_t generated_at = 0;
};

}  // namespace inkdash
