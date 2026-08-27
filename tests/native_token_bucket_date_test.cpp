#include <cassert>

#include "token_bucket_date.h"

int main() {
  using inkdash::resolveTodayYesterdayTokenBuckets;
  using inkdash::resolveRecentTokenBucketDate;

  const auto same_day = resolveRecentTokenBucketDate(2026, 8, 24, 24);
  assert(same_day.valid);
  assert(same_day.year == 2026);
  assert(same_day.month == 8);
  assert(same_day.day == 24);
  assert(same_day.age_days == 0);

  const auto yesterday = resolveRecentTokenBucketDate(2026, 8, 24, 23);
  assert(yesterday.valid);
  assert(yesterday.year == 2026);
  assert(yesterday.month == 8);
  assert(yesterday.day == 23);
  assert(yesterday.age_days == 1);

  const auto previous_month = resolveRecentTokenBucketDate(2026, 9, 1, 31);
  assert(previous_month.valid);
  assert(previous_month.year == 2026);
  assert(previous_month.month == 8);
  assert(previous_month.day == 31);
  assert(previous_month.age_days == 1);

  const auto previous_year = resolveRecentTokenBucketDate(2027, 1, 1, 31);
  assert(previous_year.valid);
  assert(previous_year.year == 2026);
  assert(previous_year.month == 12);
  assert(previous_year.day == 31);
  assert(previous_year.age_days == 1);

  const auto leap_day = resolveRecentTokenBucketDate(2028, 3, 1, 29);
  assert(leap_day.valid);
  assert(leap_day.year == 2028);
  assert(leap_day.month == 2);
  assert(leap_day.day == 29);
  assert(leap_day.age_days == 1);

  assert(!resolveRecentTokenBucketDate(2026, 8, 24, 0).valid);
  assert(!resolveRecentTokenBucketDate(2026, 8, 24, 32).valid);

  const uint8_t delayed_days[] = {17, 18, 19, 20, 21, 22, 23};
  const uint64_t delayed_tokens[] = {442, 359, 755, 323, 209, 296, 1017};
  const auto delayed = resolveTodayYesterdayTokenBuckets(
      2026, 8, 24, delayed_days, delayed_tokens, 7);
  assert(delayed.yesterday_date.valid);
  assert(delayed.yesterday_date.month == 8);
  assert(delayed.yesterday_date.day == 23);
  assert(delayed.yesterday_available);
  assert(delayed.yesterday_tokens == 1017);
  assert(delayed.today_date.valid);
  assert(delayed.today_date.month == 8);
  assert(delayed.today_date.day == 24);
  assert(!delayed.today_available);

  const uint8_t current_days[] = {18, 19, 20, 21, 22, 23, 24};
  const uint64_t current_tokens[] = {359, 755, 323, 209, 296, 1017, 587};
  const auto current = resolveTodayYesterdayTokenBuckets(
      2026, 8, 24, current_days, current_tokens, 7);
  assert(current.yesterday_available);
  assert(current.yesterday_tokens == 1017);
  assert(current.today_available);
  assert(current.today_tokens == 587);

  const uint8_t month_boundary_days[] = {25, 26, 27, 28, 29, 30, 31};
  const uint64_t month_boundary_tokens[] = {1, 2, 3, 4, 5, 6, 7};
  const auto month_boundary = resolveTodayYesterdayTokenBuckets(
      2026, 9, 1, month_boundary_days, month_boundary_tokens, 7);
  assert(month_boundary.yesterday_date.month == 8);
  assert(month_boundary.yesterday_date.day == 31);
  assert(month_boundary.yesterday_available);
  assert(month_boundary.yesterday_tokens == 7);
  assert(month_boundary.today_date.month == 9);
  assert(month_boundary.today_date.day == 1);
  assert(!month_boundary.today_available);
  return 0;
}
