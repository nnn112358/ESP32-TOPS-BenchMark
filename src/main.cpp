// M5Stack-BenchMark — ESP32 / ESP32-S3 / ESP32-P4 TOPS ベンチマーク (PIE あり / なし 比較)
// Copyright (c) 2026 nnn112358 <https://github.com/nnn112358>
// SPDX-License-Identifier: MIT
// Developed with Claude Code (Anthropic).
//
//   PIE SIMD 命令 (ee.* / esp.*) と通常の C スカラーコードで同じ処理を行い、
//   スループット (GOPS / TOPS) と倍率を M5Unified の画面とシリアルに表示する。
//   画面: 1 コアでの PIE / noPIE / 倍率。シリアル: 2 コア同時実行の合計と CSV も出力。
//   再測定: 画面タップ / ボタン / シリアルに 'r'。
//
//   モジュール構成:
//     kernels.h + pie_kernels.S / pie_kernels_p4.S / c_kernels.cpp  … 計測カーネル
//     buffers    … 計測用バッファ (SRAM / PSRAM)
//     benchmarks … 測定項目の表 (PIE 版 / noPIE 版のペア)
//     measure    … キャリブレーションと 1 コア / 2 コア計測
//     display    … M5Unified 画面表示
//     report     … シリアル出力 (表 + CSV)

#include <M5Unified.h>
#include "benchmarks.h"
#include "buffers.h"
#include "display.h"
#include "measure.h"
#include "report.h"

static buffers::Set g_buf[2];   // [0] = メインコア, [1] = ワーカーコア
static bool         g_psram_ok = false;

static void run_all() {
  display::draw_header(g_psram_ok);
  for (int i = 0; i < bench::count(); ++i) display::draw_row(i, display::RowState::Pending, g_psram_ok);
  report::header(g_psram_ok);

  for (int i = 0; i < bench::count(); ++i) {
    bench::Item& b = bench::items()[i];
    display::draw_row(i, display::RowState::Running, g_psram_ok);
    if (b.has_pie(g_psram_ok))   b.r_pie   = measure::run(b.pie,   b.work_per_iter, b.unit);
    if (b.has_nopie(g_psram_ok)) b.r_nopie = measure::run(b.nopie, b.work_per_iter, b.unit);
    display::draw_row(i, display::RowState::Done, g_psram_ok);
    report::row(i, g_psram_ok);
  }

  display::draw_summary(g_psram_ok);
  report::footer(g_psram_ok);
  while (Serial.available()) Serial.read();   // 計測中に届いたコマンドは捨てる
}

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(115200);
  display::begin();

  if (!buffers::allocate(g_buf[0], 1234) || !buffers::allocate(g_buf[1], 5678)) {
    display::draw_error("buffer alloc failed");
    Serial.println("buffer alloc failed");
    for (;;) delay(1000);
  }
  g_psram_ok = g_buf[0].has_psram() && g_buf[1].has_psram();

  measure::init(g_buf);
  run_all();
}

void loop() {
  M5.update();
  bool rerun = M5.Touch.getDetail().wasClicked()
            || M5.BtnA.wasClicked() || M5.BtnB.wasClicked() || M5.BtnC.wasClicked();
  while (Serial.available()) {
    const int ch = Serial.read();
    if (ch == 'r' || ch == 'R') rerun = true;   // PC から "r" を送ると再測定
  }
  if (rerun) run_all();
  delay(20);
}
