// ESP32-TOPS-BenchMark — 計測用バッファ (内部 SRAM / PSRAM)
// Copyright (c) 2026 nnn112358 <https://github.com/nnn112358>
// SPDX-License-Identifier: MIT
// Developed with Claude Code (Anthropic).
#include "buffers.h"
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <string.h>

namespace buffers {

static void fill_pattern(uint8_t* p, size_t n, uint32_t seed) {
  uint32_t x = seed;   // LCG。値の中身は速度に影響しないが、演算が本物になるよう乱数にしておく
  for (size_t i = 0; i < n; ++i) {
    x = x * 1664525u + 1013904223u;
    p[i] = (uint8_t)(x >> 24);
  }
}

static uint8_t* alloc16(size_t size, uint32_t caps) {
  return (uint8_t*)heap_caps_aligned_alloc(VEC, size, caps);
}

bool allocate(Set& s, uint32_t seed) {
  const uint32_t internal = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
  s.sram_a = alloc16(SRAM_SIZE + SLACK, internal);
  s.sram_b = alloc16(SRAM_SIZE, internal);
  if (!s.sram_a || !s.sram_b) return false;
  fill_pattern(s.sram_a, SRAM_SIZE + SLACK, seed);
  memset(s.sram_b, 0, SRAM_SIZE);

  if (psramFound()) {
    s.psram_a = alloc16(PSRAM_SIZE + SLACK, MALLOC_CAP_SPIRAM);
    s.psram_b = alloc16(PSRAM_SIZE, MALLOC_CAP_SPIRAM);
    if (s.has_psram()) {
      fill_pattern(s.psram_a, PSRAM_SIZE + SLACK, seed ^ 0x5a5a);
      memset(s.psram_b, 0, PSRAM_SIZE);
    } else {
      s.psram_a = s.psram_b = nullptr;
    }
  }
  return true;
}

}  // namespace buffers
