// M5Stack-BenchMark — M5Unified 画面表示
// Copyright (c) 2026 nnn112358 <https://github.com/nnn112358>
// SPDX-License-Identifier: MIT
// Developed with Claude Code (Anthropic).
#pragma once

namespace display {

enum class RowState { Pending, Running, Done };

void begin();                          // M5.Display の初期化 (回転、明るさ)
void draw_header(bool psram_ok);       // 画面クリア + チップ情報 + 表の見出し (画面サイズから拡大率も決める)
void draw_row(int index, RowState st, bool psram_ok);
void draw_summary(bool psram_ok);
void draw_error(const char* msg);

}  // namespace display
