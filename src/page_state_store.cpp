#include "page_state_store.h"

#include <esp_spi_flash.h>
#include <esp_system.h>
#include <string.h>

#include "flash_layout.h"
#include "page_restart_policy.h"

namespace inkdash {
namespace {

constexpr size_t kSlotOffsets[] = {flash::kPageStateSlotA,
                                   flash::kPageStateSlotB};
static_assert(sizeof(PageStateRecord) <= flash::kSectorBytes,
              "A page-state record must fit in one flash sector");

bool isCaseLikeReset(esp_reset_reason_t reason) {
  return reason == ESP_RST_POWERON || reason == ESP_RST_EXT ||
         reason == ESP_RST_UNKNOWN;
}

}  // namespace

PageStartupState PageStateStore::begin(size_t page_count) {
  page_count_ = page_count;
  const esp_reset_reason_t reset_reason = esp_reset_reason();
  partition_ = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                        ESP_PARTITION_SUBTYPE_DATA_SPIFFS,
                                        "spiffs");
  if (partition_ == nullptr || partition_->size < flash::kReservedEnd ||
      page_count_ == 0) {
    Serial.println("Page state storage unavailable; starting on Codex");
    partition_ = nullptr;
    return PageStartupState{0, false, false, false,
                            static_cast<int>(reset_reason)};
  }
  mounted_ = true;

  PageStateRecord first{};
  PageStateRecord second{};
  const bool first_read = readSlot(0, first);
  const bool second_read = readSlot(1, second);
  const PageStateRecord* selected = newestValidPageState(
      first_read ? &first : nullptr, second_read ? &second : nullptr,
      page_count_);
  const bool had_valid_record = selected != nullptr;
  if (had_valid_record) {
    record_ = *selected;
    active_slot_ = selected == &second ? 1 : 0;
    Serial.printf("Page state restored: sequence=%lu page=%u slot=%c\n",
                  static_cast<unsigned long>(record_.sequence),
                  static_cast<unsigned>(record_.page_index + 1),
                  active_slot_ == 0 ? 'A' : 'B');
  } else {
    record_ = PageStateRecord{};
    active_slot_ = -1;
    Serial.println("Page state storage ready: no previous selection");
  }

  const PageBootDecision decision = choosePageOnBoot(
      had_valid_record ? record_.page_index : 0, had_valid_record,
      isCaseLikeReset(reset_reason), page_count_);
  const bool write_succeeded =
      decision.initialize_record || decision.case_button_restart
          ? save(decision.page_index)
          : true;
  if (!write_succeeded) {
    Serial.println("Page state write failed; previous A/B copy retained");
  }
  return PageStartupState{decision.page_index,
                          decision.case_button_restart,
                          had_valid_record,
                          write_succeeded,
                          static_cast<int>(reset_reason)};
}

bool PageStateStore::save(size_t page_index) {
  if (!mounted_ || partition_ == nullptr || page_count_ == 0) {
    return false;
  }
  const uint8_t normalized =
      static_cast<uint8_t>(page_index % page_count_);
  if (active_slot_ >= 0 && record_.page_index == normalized) {
    return true;
  }

  PageStateRecord candidate = active_slot_ >= 0 ? record_ : PageStateRecord{};
  candidate.sequence = active_slot_ < 0 || candidate.sequence == UINT32_MAX
                           ? 1
                           : candidate.sequence + 1;
  candidate.page_index = normalized;
  finalizePageStateRecord(candidate);
  return writeCandidate(candidate);
}

bool PageStateStore::readSlot(uint8_t slot, PageStateRecord& output) const {
  if (!mounted_ || partition_ == nullptr || slot > 1) {
    return false;
  }
  const void* mapped = nullptr;
  spi_flash_mmap_handle_t map_handle = 0;
  const esp_err_t map_result = esp_partition_mmap(
      partition_, kSlotOffsets[slot], sizeof(output), SPI_FLASH_MMAP_DATA,
      &mapped, &map_handle);
  if (map_result != ESP_OK || mapped == nullptr) {
    return false;
  }
  memcpy(&output, mapped, sizeof(output));
  spi_flash_munmap(map_handle);
  return validPageStateRecord(output, page_count_);
}

bool PageStateStore::writeCandidate(const PageStateRecord& candidate) {
  if (!mounted_ || partition_ == nullptr ||
      !validPageStateRecord(candidate, page_count_)) {
    return false;
  }
  const uint8_t target_slot = active_slot_ == 0 ? 1 : 0;
  const size_t target_offset = kSlotOffsets[target_slot];
  const esp_err_t erase_result =
      esp_partition_erase_range(partition_, target_offset, flash::kSectorBytes);
  if (erase_result != ESP_OK) {
    Serial.printf("Page state erase failed: %d\n",
                  static_cast<int>(erase_result));
    return false;
  }
  const esp_err_t write_result = esp_partition_write(
      partition_, target_offset, &candidate, sizeof(candidate));
  if (write_result != ESP_OK) {
    Serial.printf("Page state flash write failed: %d\n",
                  static_cast<int>(write_result));
    return false;
  }
  PageStateRecord verified{};
  if (!readSlot(target_slot, verified) ||
      memcmp(&candidate, &verified, sizeof(candidate)) != 0) {
    Serial.println("Page state readback validation failed");
    return false;
  }
  record_ = verified;
  active_slot_ = target_slot;
  Serial.printf("Page state saved: sequence=%lu page=%u slot=%c\n",
                static_cast<unsigned long>(record_.sequence),
                static_cast<unsigned>(record_.page_index + 1),
                active_slot_ == 0 ? 'A' : 'B');
  return true;
}

}  // namespace inkdash
