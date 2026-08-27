#pragma once

#include <string.h>

#include "dashboard_data.h"

namespace inkdash {
namespace detail {

constexpr uint64_t visibleTokenCentiYi(uint64_t tokens) {
  return tokens / 1000000ULL +
         (tokens % 1000000ULL >= 500000ULL ? 1ULL : 0ULL);
}

constexpr uint64_t hongKongDay(uint32_t epoch) {
  return (static_cast<uint64_t>(epoch) + 8ULL * 60ULL * 60ULL) /
         (24ULL * 60ULL * 60ULL);
}

}  // namespace detail

// Compare only pixels derived from Codex data. A fresh generated_at heartbeat
// within the same Hong Kong date must not cause a costly tri-color refresh.
inline bool codexDisplayChanged(const CodexDashboardData& displayed,
                                DataStatus displayed_status,
                                const CodexDashboardData& fetched,
                                DataStatus fetched_status) {
  if (displayed_status != fetched_status ||
      displayed.remaining_percent != fetched.remaining_percent ||
      displayed.used_percent != fetched.used_percent ||
      displayed.reset_at / 60U != fetched.reset_at / 60U ||
      detail::hongKongDay(displayed.generated_at) !=
          detail::hongKongDay(fetched.generated_at) ||
      detail::visibleTokenCentiYi(displayed.today_tokens) !=
          detail::visibleTokenCentiYi(fetched.today_tokens) ||
      detail::visibleTokenCentiYi(displayed.week_tokens) !=
          detail::visibleTokenCentiYi(fetched.week_tokens) ||
      detail::visibleTokenCentiYi(displayed.cumulative_tokens) !=
          detail::visibleTokenCentiYi(fetched.cumulative_tokens) ||
      memcmp(displayed.daily_day_of_month, fetched.daily_day_of_month,
             sizeof(displayed.daily_day_of_month)) != 0 ||
      memcmp(displayed.daily_usage_centi_yi, fetched.daily_usage_centi_yi,
             sizeof(displayed.daily_usage_centi_yi)) != 0) {
    return true;
  }
  return false;
}

}  // namespace inkdash
