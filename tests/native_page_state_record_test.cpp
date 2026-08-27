#include <cassert>

#include "page_state_record.h"

int main() {
  using inkdash::PageStateRecord;
  using inkdash::finalizePageStateRecord;
  using inkdash::newestValidPageState;
  using inkdash::validPageStateRecord;

  PageStateRecord first{};
  first.sequence = 1;
  first.page_index = 0;
  finalizePageStateRecord(first);
  assert(validPageStateRecord(first, 4));

  PageStateRecord second = first;
  second.sequence = 2;
  second.page_index = 1;
  finalizePageStateRecord(second);
  assert(validPageStateRecord(second, 4));
  assert(newestValidPageState(&first, &second, 4) == &second);

  PageStateRecord wallpaper = second;
  wallpaper.sequence = 3;
  wallpaper.page_index = 2;
  finalizePageStateRecord(wallpaper);
  assert(validPageStateRecord(wallpaper, 4));
  assert(newestValidPageState(&second, &wallpaper, 4) == &wallpaper);

  PageStateRecord health = wallpaper;
  health.sequence = 4;
  health.page_index = 3;
  finalizePageStateRecord(health);
  assert(validPageStateRecord(health, 4));
  assert(newestValidPageState(&wallpaper, &health, 4) == &health);

  PageStateRecord corrupt = second;
  corrupt.page_index = 0;
  assert(!validPageStateRecord(corrupt, 4));
  assert(newestValidPageState(&first, &corrupt, 4) == &first);

  PageStateRecord out_of_range = second;
  out_of_range.page_index = 4;
  finalizePageStateRecord(out_of_range);
  assert(!validPageStateRecord(out_of_range, 4));

  return 0;
}
