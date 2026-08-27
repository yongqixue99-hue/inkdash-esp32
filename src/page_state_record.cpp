#include "page_state_record.h"

#include <stddef.h>
#include <string.h>

namespace inkdash {
namespace {

constexpr uint32_t kPageStateMagic = 0x45474150;  // "PAGE"
constexpr uint16_t kPageStateVersion = 1;

uint32_t checksum(const PageStateRecord& record) {
  const auto* bytes = reinterpret_cast<const uint8_t*>(&record);
  uint32_t value = 2166136261u;
  for (size_t index = 0; index < offsetof(PageStateRecord, checksum); ++index) {
    value ^= bytes[index];
    value *= 16777619u;
  }
  return value;
}

bool sequenceAfter(uint32_t candidate, uint32_t reference) {
  return static_cast<int32_t>(candidate - reference) > 0;
}

}  // namespace

void finalizePageStateRecord(PageStateRecord& record) {
  record.magic = kPageStateMagic;
  record.version = kPageStateVersion;
  record.record_size = sizeof(record);
  record.page_index_inverse = static_cast<uint8_t>(~record.page_index);
  memset(record.reserved, 0, sizeof(record.reserved));
  record.checksum = 0;
  record.checksum = checksum(record);
}

bool validPageStateRecord(const PageStateRecord& record, size_t page_count) {
  return page_count > 0 && record.magic == kPageStateMagic &&
         record.version == kPageStateVersion &&
         record.record_size == sizeof(record) && record.sequence != 0 &&
         record.page_index < page_count &&
         record.page_index_inverse ==
             static_cast<uint8_t>(~record.page_index) &&
         record.checksum == checksum(record);
}

const PageStateRecord* newestValidPageState(
    const PageStateRecord* first, const PageStateRecord* second,
    size_t page_count) {
  const bool first_valid =
      first != nullptr && validPageStateRecord(*first, page_count);
  const bool second_valid =
      second != nullptr && validPageStateRecord(*second, page_count);
  if (!first_valid) {
    return second_valid ? second : nullptr;
  }
  if (!second_valid) {
    return first;
  }
  return sequenceAfter(second->sequence, first->sequence) ? second : first;
}

}  // namespace inkdash
