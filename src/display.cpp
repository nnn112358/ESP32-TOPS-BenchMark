// M5Stack-BenchMark — M5Unified 画面表示
// Copyright (c) 2026 nnn112358 <https://github.com/nnn112358>
// SPDX-License-Identifier: MIT
// Developed with Claude Code (Anthropic).
#include "display.h"
#include <M5Unified.h>
#include "benchmarks.h"
#include "kernels.h"

namespace display {

namespace {

// 320x240 を基準にしたレイアウト。大きい画面 (Tab5 1280x720 など) は整数倍に拡大する
int g_scale = 1;

constexpr int BASE_W = 320, BASE_H = 240;
int row_y0()  { return 44 * g_scale; }   // 表の先頭 y
int row_h()   { return  8 * g_scale; }   // Font0 の行高
int summary_y() { return row_y0() + bench::count() * row_h() + 4 * g_scale; }

void fmt_val(char* buf, size_t n, double v, bool valid, bench::Unit u) {
  if (!valid)                      snprintf(buf, n, "%8s", "-");
  else if (u == bench::Unit::MBPS) snprintf(buf, n, "%8.1f", v);
  else                             snprintf(buf, n, "%8.3f", v);
}

}  // namespace

void begin() {
  M5.Display.setRotation(1);
  M5.Display.setBrightness(128);
}

void draw_header(bool psram_ok) {
  auto& d = M5.Display;
  const int sx = d.width() / BASE_W, sy = d.height() / BASE_H;
  g_scale = (sx < sy) ? sx : sy;
  if (g_scale < 1) g_scale = 1;
  d.setTextSize(g_scale);
  d.fillScreen(TFT_BLACK);

  d.setFont(&fonts::Font2);
  d.setTextColor(TFT_WHITE, TFT_BLACK);
  d.setCursor(0, 0);
  d.printf("%s rev%d %luMHz %s", ESP.getChipModel(), ESP.getChipRevision(),
           (unsigned long)getCpuFrequencyMhz(), HAS_PIE ? "PIE vs noPIE" : "(no PIE)");
  d.setCursor(0, 16 * g_scale);
  d.printf("1 core, PSRAM %luMB, MAC = 2 ops", (unsigned long)(ESP.getPsramSize() >> 20));

  d.setFont(&fonts::Font0);
  d.setTextColor(TFT_YELLOW, TFT_BLACK);
  d.setCursor(0, 34 * g_scale);
  d.printf("%-15s %8s %8s %7s %s", "test", "PIE", "noPIE", "ratio", "unit");
  (void)psram_ok;
}

void draw_row(int index, RowState st, bool psram_ok) {
  auto& d = M5.Display;
  const bench::Item& b = bench::items()[index];
  const int y = row_y0() + index * row_h();
  d.setFont(&fonts::Font0);
  d.fillRect(0, y, d.width(), row_h(), TFT_BLACK);
  d.setCursor(0, y);
  switch (st) {
    case RowState::Pending:
      d.setTextColor(TFT_DARKGREY, TFT_BLACK);
      d.printf("%-15s", b.name);
      break;
    case RowState::Running:
      d.setTextColor(TFT_CYAN, TFT_BLACK);
      d.printf("%-15s %8s", b.name, "...");
      break;
    case RowState::Done: {
      char sp[16], sn[16], sr[16];
      fmt_val(sp, sizeof sp, b.r_pie.one_core,   b.has_pie(psram_ok),   b.unit);
      fmt_val(sn, sizeof sn, b.r_nopie.one_core, b.has_nopie(psram_ok), b.unit);
      if (b.has_ratio(psram_ok)) snprintf(sr, sizeof sr, "%6.1fx", b.ratio());
      else                       snprintf(sr, sizeof sr, "%7s", "-");
      d.setTextColor(TFT_WHITE, TFT_BLACK);
      d.printf("%-15s %s %s %s %s", b.name, sp, sn, sr, bench::unit_str(b.unit));
      break;
    }
  }
}

void draw_summary(bool psram_ok) {
  auto& d = M5.Display;
  const bench::Peak dev = bench::peak_device(psram_ok);
  const bench::Peak n   = bench::peak_of(false, psram_ok);
  const int y = summary_y();
  d.fillRect(0, y, d.width(), d.height() - y, TFT_BLACK);

  d.setFont(&fonts::Font2);
  d.setTextColor(TFT_GREEN, TFT_BLACK);
  d.setCursor(0, y);
  d.printf("PEAK %.2f GOPS = %.4f TOPS", dev.gops, dev.tops());

  d.setFont(&fonts::Font0);
  d.setTextColor(TFT_WHITE, TFT_BLACK);
  d.setCursor(0, y + 18 * g_scale);
  d.printf("= %s %s, 2 cores", dev.kind, dev.test);
  d.setCursor(0, y + 27 * g_scale);
  if (HAS_PIE) d.printf("noPIE best: %.2f GOPS (%s)", n.gops, n.test);
  else         d.printf("%s has no PIE SIMD (scalar only)", ESP.getChipModel());

  d.setTextColor(TFT_DARKGREY, TFT_BLACK);
  d.setCursor(0, y + 36 * g_scale);
  d.print("tap screen to re-run");
}

void draw_error(const char* msg) {
  auto& d = M5.Display;
  d.setFont(&fonts::Font2);
  d.setTextColor(TFT_RED, TFT_BLACK);
  d.setCursor(0, 0);
  d.println(msg);
}

}  // namespace display
