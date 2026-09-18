// M5Stack-BenchMark — 計測用バッファ (内部 SRAM / PSRAM)
// Copyright (c) 2026 nnn112358 <https://github.com/nnn112358>
// SPDX-License-Identifier: MIT
// Developed with Claude Code (Anthropic).
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace buffers {

constexpr size_t SRAM_SIZE  = 16 * 1024;    // 内部 SRAM: a (前半 = 入力 a, 後半 = 入力 b), b (出力 / コピー先)
constexpr size_t PSRAM_SIZE = 512 * 1024;   // PSRAM:     同じ構成
constexpr size_t SRAM_HALF  = SRAM_SIZE / 2;
constexpr size_t PSRAM_HALF = PSRAM_SIZE / 2;
constexpr size_t VEC        = 16;           // PIE の 1 ベクトル = 128 bit
constexpr size_t SLACK      = VEC;          // 内積カーネルの 1 ベクトル先読み用余白

// 1 コア分のバッファ。すべて 16 バイト境界
struct Set {
  uint8_t* sram_a  = nullptr;   // SRAM_SIZE + SLACK, 乱数で初期化
  uint8_t* sram_b  = nullptr;   // SRAM_SIZE
  uint8_t* psram_a = nullptr;   // PSRAM_SIZE + SLACK (PSRAM の無い機種では nullptr)
  uint8_t* psram_b = nullptr;   // PSRAM_SIZE

  bool has_psram() const { return psram_a && psram_b; }
};

// 確保して初期化する。SRAM が取れなければ false。PSRAM は無ければ nullptr のまま true
bool allocate(Set& s, uint32_t seed);

}  // namespace buffers
