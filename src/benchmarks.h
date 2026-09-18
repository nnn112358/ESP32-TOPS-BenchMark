// M5Stack-BenchMark — 測定項目の定義 (PIE 版 / noPIE 版カーネルのペア)
// Copyright (c) 2026 nnn112358 <https://github.com/nnn112358>
// SPDX-License-Identifier: MIT
// Developed with Claude Code (Anthropic).
#pragma once
#include <stdint.h>
#include "buffers.h"

namespace bench {

enum class Unit { GOPS, GFLOPS, MBPS };
const char* unit_str(Unit u);

// 1 項目のカーネル: iters 回実行し、最適化除けのチェックサムを返す
using KernelFn = uint32_t (*)(uint32_t iters, buffers::Set* buf);

struct Result {
  double one_core = 0;    // 1 コアの最良値
  double two_core = 0;    // 2 コア同時実行の合計の最良値
};

struct Item {
  const char* name;
  KernelFn    pie;             // nullptr = PIE 版なし (機種に PIE が無い、または項目に PIE 版が無い)
  KernelFn    nopie;           // nullptr = noPIE 版なし
  double      work_per_iter;   // 1 イテレーションの演算数 (ops) またはバイト数 (両版で同じ)
  Unit        unit;
  bool        need_psram;      // PSRAM の無い機種ではスキップ
  Result      r_pie;
  Result      r_nopie;

  bool has_pie(bool psram_ok) const   { return pie   && (!need_psram || psram_ok); }
  bool has_nopie(bool psram_ok) const { return nopie && (!need_psram || psram_ok); }
  bool has_ratio(bool psram_ok) const { return has_pie(psram_ok) && has_nopie(psram_ok) && r_nopie.one_core > 0; }
  double ratio() const                { return r_pie.one_core / r_nopie.one_core; }
};

Item* items();
int   count();

// GOPS 項目の 2 コア合計の最大値と、それを出した項目
struct Peak {
  double      gops = 0;
  const char* test = "-";
  const char* kind = "-";   // "PIE" / "noPIE"
  double tops() const { return gops / 1000.0; }
};
Peak peak_of(bool pie_side, bool psram_ok);
Peak peak_device(bool psram_ok);   // PIE / noPIE の大きい方

}  // namespace bench
