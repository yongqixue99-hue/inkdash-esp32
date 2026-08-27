#pragma once

#include <stdint.h>

#include "esp_partition.h"

using spi_flash_mmap_handle_t = uintptr_t;

enum spi_flash_mmap_memory_t {
  SPI_FLASH_MMAP_DATA,
};

esp_err_t esp_partition_mmap(const esp_partition_t* partition, size_t offset,
                             size_t size, spi_flash_mmap_memory_t memory,
                             const void** mapped,
                             spi_flash_mmap_handle_t* handle);
void spi_flash_munmap(spi_flash_mmap_handle_t handle);
