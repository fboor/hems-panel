// Large, long-lived UI buffers (the 24 h history and the chart series) belong
// in PSRAM. Internal DRAM is what Wi-Fi, HTTP and the LVGL draw buffer need,
// and none of these arrays is touched by an ISR or by the LCD DMA.
//
// The ESP32-S3 Arduino core this project builds against does not allow BSS in
// external RAM (CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY is off), so
// EXT_RAM_BSS_ATTR would silently do nothing. Allocate once at startup
// instead, and fall back to internal RAM so a board without PSRAM loses heap,
// not the feature.
//
// Shared by firmware and emulator; the emulator has no PSRAM and uses calloc.
//
// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <cstdlib>

#ifdef ARDUINO
#include <esp_heap_caps.h>
#endif

namespace hemsui {

// Zeroed block of `bytes`, PSRAM first, internal RAM second, nullptr last.
inline void *allocLarge(size_t bytes) {
#ifdef ARDUINO
  void *p = heap_caps_calloc(1, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (p != nullptr) {
    return p;
  }
  return heap_caps_calloc(1, bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
#else
  return std::calloc(1, bytes);
#endif
}

}  // namespace hemsui
