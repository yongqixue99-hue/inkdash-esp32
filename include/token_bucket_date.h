#pragma once

#include <stddef.h>
#include <stdint.h>

namespace inkdash {

struct TokenBucketDate {
  TokenBucketDate(uint16_t year_value = 0, uint8_t month_value = 0,
                  uint8_t day_value = 0, uint8_t age_value = 0,
                  bool valid_value = false)
      : year(year_value),
        month(month_value),
        day(day_value),
        age_days(age_value),
        valid(valid_value) {}

  uint16_t year;
  uint8_t month;
  uint8_t day;
  uint8_t age_days;
  bool valid;
};

struct TodayYesterdayTokenBuckets {
  TodayYesterdayTokenBuckets()
      : today_tokens(0),
        yesterday_tokens(0),
        today_available(false),
        yesterday_available(false) {}

  TokenBucketDate today_date;
  TokenBucketDate yesterday_date;
  uint64_t today_tokens;
  uint64_t yesterday_tokens;
  bool today_available;
  bool yesterday_available;
};

constexpr bool tokenBucketLeapYear(uint16_t year) {
  return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

inline uint8_t tokenBucketDaysInMonth(uint16_t year, uint8_t month) {
  switch (month) {
    case 2:
      return tokenBucketLeapYear(year) ? 29 : 28;
    case 4:
    case 6:
    case 9:
    case 11:
      return 30;
    case 1:
    case 3:
    case 5:
    case 7:
    case 8:
    case 10:
    case 12:
      return 31;
    default:
      return 0;
  }
}

inline TokenBucketDate tokenBucketDateDaysAgo(
    uint16_t current_year, uint8_t current_month, uint8_t current_day,
    uint8_t age_days) {
  if (current_year < 2000 || current_month < 1 || current_month > 12 ||
      current_day < 1 ||
      current_day > tokenBucketDaysInMonth(current_year, current_month)) {
    return TokenBucketDate();
  }
  uint16_t year = current_year;
  uint8_t month = current_month;
  uint8_t day = current_day;
  for (uint8_t age = 0; age < age_days; ++age) {
    if (day > 1) {
      --day;
      continue;
    }
    if (month == 1) {
      if (year == 2000) {
        return TokenBucketDate();
      }
      --year;
      month = 12;
    } else {
      --month;
    }
    day = tokenBucketDaysInMonth(year, month);
  }
  return TokenBucketDate(year, month, day, age_days, true);
}

// The account profile exposes completed ISO daily buckets, which can trail the
// fetch date. The compact firmware snapshot retains each bucket's day-of-month;
// resolve it against the snapshot generation date without assuming it means
// "today". The latest account bucket is expected to be recent, so one month of
// backwards search is both sufficient and rejects corrupt/stale day values.
inline TokenBucketDate resolveRecentTokenBucketDate(
    uint16_t current_year, uint8_t current_month, uint8_t current_day,
    uint8_t bucket_day) {
  if (current_year < 2000 || current_month < 1 || current_month > 12 ||
      current_day < 1 ||
      current_day > tokenBucketDaysInMonth(current_year, current_month) ||
      bucket_day < 1 || bucket_day > 31) {
    return TokenBucketDate();
  }

  uint16_t year = current_year;
  uint8_t month = current_month;
  uint8_t day = current_day;
  for (uint8_t age = 0; age <= 31; ++age) {
    if (day == bucket_day) {
      return TokenBucketDate(year, month, day, age, true);
    }
    if (day > 1) {
      --day;
      continue;
    }
    if (month == 1) {
      if (year == 2000) {
        return TokenBucketDate();
      }
      --year;
      month = 12;
    } else {
      --month;
    }
    day = tokenBucketDaysInMonth(year, month);
  }
  return TokenBucketDate();
}

inline TodayYesterdayTokenBuckets resolveTodayYesterdayTokenBuckets(
    uint16_t current_year, uint8_t current_month, uint8_t current_day,
    const uint8_t* bucket_days, const uint64_t* bucket_tokens,
    size_t bucket_count) {
  TodayYesterdayTokenBuckets result;
  result.today_date = tokenBucketDateDaysAgo(
      current_year, current_month, current_day, 0);
  result.yesterday_date = tokenBucketDateDaysAgo(
      current_year, current_month, current_day, 1);
  if (!result.today_date.valid || !result.yesterday_date.valid ||
      bucket_days == nullptr || bucket_tokens == nullptr) {
    return result;
  }
  for (size_t index = 0; index < bucket_count; ++index) {
    const TokenBucketDate bucket = resolveRecentTokenBucketDate(
        current_year, current_month, current_day, bucket_days[index]);
    if (!bucket.valid) {
      continue;
    }
    if (bucket.age_days == 0) {
      result.today_tokens = bucket_tokens[index];
      result.today_available = true;
    } else if (bucket.age_days == 1) {
      result.yesterday_tokens = bucket_tokens[index];
      result.yesterday_available = true;
    }
  }
  return result;
}

}  // namespace inkdash
