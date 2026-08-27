#include <cassert>
#include <cstddef>

#include "page_restart_policy.h"

int main() {
  using inkdash::choosePageOnBoot;

  constexpr std::size_t kPageCount = 4;

  // The first boot after installing this scheme establishes Codex as page 0;
  // it must not unexpectedly jump to the server page just because no record
  // exists yet.
  {
    const auto decision = choosePageOnBoot(0, false, true, kPageCount);
    assert(decision.page_index == 0);
    assert(!decision.case_button_restart);
    assert(decision.initialize_record);
  }

  // Once valid, EN/RST cycles Codex -> server -> wallpaper -> health.
  {
    const auto decision = choosePageOnBoot(0, true, true, kPageCount);
    assert(decision.page_index == 1);
    assert(decision.case_button_restart);
    assert(!decision.initialize_record);
  }
  {
    const auto decision = choosePageOnBoot(1, true, true, kPageCount);
    assert(decision.page_index == 2);
    assert(decision.case_button_restart);
  }
  {
    const auto decision = choosePageOnBoot(2, true, true, kPageCount);
    assert(decision.page_index == 3);
    assert(decision.case_button_restart);
  }
  {
    const auto decision = choosePageOnBoot(3, true, true, kPageCount);
    assert(decision.page_index == 0);
    assert(decision.case_button_restart);
  }

  // Software/watchdog/brownout restarts preserve the visible page.
  {
    const auto decision = choosePageOnBoot(3, true, false, kPageCount);
    assert(decision.page_index == 3);
    assert(!decision.case_button_restart);
  }

  // Corrupt persisted indices are normalized before use.
  {
    const auto decision = choosePageOnBoot(8, true, false, kPageCount);
    assert(decision.page_index == 0);
  }

  return 0;
}
