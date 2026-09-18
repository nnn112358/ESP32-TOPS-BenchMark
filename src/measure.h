// M5Stack-BenchMark — 計測 (キャリブレーション、1 コア / 2 コア同時実行)
// Copyright (c) 2026 nnn112358 <https://github.com/nnn112358>
// SPDX-License-Identifier: MIT
// Developed with Claude Code (Anthropic).
#pragma once
#include "benchmarks.h"
#include "buffers.h"

namespace measure {

// buf[0] = メインコア用, buf[1] = ワーカーコア (反対側のコア) 用。ワーカータスクを起動する
void init(buffers::Set* buf);

// 1 項目のカーネルを計測する: ~100 ms になるイテレーション数を求め、
// 1 コア単独と 2 コア同時 (各コアの実測スループットの合計) をそれぞれ 2 回測って最良値を返す
bench::Result run(bench::KernelFn fn, double work_per_iter, bench::Unit unit);

}  // namespace measure
