#pragma once

#include <cstddef>

namespace inkdash {

struct PageBootDecision {
  constexpr PageBootDecision(std::size_t selected_page = 0,
                             bool from_case_button = false,
                             bool should_initialize = false)
      : page_index(selected_page),
        case_button_restart(from_case_button),
        initialize_record(should_initialize) {}

  std::size_t page_index;
  bool case_button_restart;
  bool initialize_record;
};

constexpr std::size_t normalizePageIndex(std::size_t page_index,
                                         std::size_t page_count) {
  return page_count == 0 ? 0 : page_index % page_count;
}

// EN/RST stops the CPU before firmware can poll the physical switch. Once a
// valid flash record exists, a case-like reset is therefore interpreted during
// the following boot as one page advance. The first install initializes page 0
// without advancing so flashing never lands unexpectedly on page 2.
constexpr PageBootDecision choosePageOnBoot(
    std::size_t persisted_page, bool has_valid_record,
    bool case_like_reset, std::size_t page_count) {
  return page_count == 0
             ? PageBootDecision()
             : !has_valid_record
                   ? PageBootDecision(0, false, true)
                   : case_like_reset
                         ? PageBootDecision(
                               (normalizePageIndex(persisted_page,
                                                   page_count) +
                                1) %
                                   page_count,
                               true, false)
                         : PageBootDecision(
                               normalizePageIndex(persisted_page, page_count),
                               false, false);
}

}  // namespace inkdash
