// M5Stack-BenchMark — シリアル出力 (人間向けの表と PC 取得用 CSV)
// Copyright (c) 2026 nnn112358 <https://github.com/nnn112358>
// SPDX-License-Identifier: MIT
// Developed with Claude Code (Anthropic).
#pragma once

namespace report {

void header(bool psram_ok);        // device 行と列見出し
void row(int index, bool psram_ok);
void footer(bool psram_ok);        // ピーク行 + "=== CSV ===" 〜 "=== END ===" ブロック

}  // namespace report
