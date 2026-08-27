#pragma once

#include <stddef.h>
#include <stdint.h>

namespace inkdash {

struct PageStateRecord {
  uint32_t magic = 0;
  uint16_t version = 0;
  uint16_t record_size = 0;
  uint32_t sequence = 0;
  uint8_t page_index = 0;
  uint8_t page_index_inverse = 0;
  uint8_t reserved[2]{};
  uint32_t checksum = 0;
};

void finalizePageStateRecord(PageStateRecord& record);
bool validPageStateRecord(const PageStateRecord& record, size_t page_count);
const PageStateRecord* newestValidPageState(
    const PageStateRecord* first, const PageStateRecord* second,
    size_t page_count);

}  // namespace inkdash
