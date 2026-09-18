// ESP32-TOPS-BenchMark — シリアル出力 (人間向けの表と PC 取得用 CSV)
// Copyright (c) 2026 nnn112358 <https://github.com/nnn112358>
// SPDX-License-Identifier: MIT
// Developed with Claude Code (Anthropic).
#include "report.h"
#include <Arduino.h>
#include "benchmarks.h"
#include "kernels.h"

namespace report {

namespace {

void fmt_val(char* buf, size_t n, double v, bool valid, bench::Unit u) {
  if (!valid)                      snprintf(buf, n, "%9s", "-");
  else if (u == bench::Unit::MBPS) snprintf(buf, n, "%9.1f", v);
  else                             snprintf(buf, n, "%9.3f", v);
}

void csv_val(char* buf, size_t n, double v, bool valid) {
  if (valid) snprintf(buf, n, "%.4f", v); else buf[0] = 0;
}

}  // namespace

void header(bool psram_ok) {
  (void)psram_ok;
  Serial.printf("\ndevice       = %s rev%d, %lu MHz, 2 cores, PSRAM %lu MB\n",
                ESP.getChipModel(), ESP.getChipRevision(), (unsigned long)getCpuFrequencyMhz(),
                (unsigned long)(ESP.getPsramSize() >> 20));
  Serial.printf("%-16s %9s %9s %9s %9s %7s  unit\n", "", "PIE-1c", "noPIE-1c", "PIE-2c", "noPIE-2c", "ratio");
}

void row(int index, bool psram_ok) {
  const bench::Item& b = bench::items()[index];
  char s[4][16];
  fmt_val(s[0], 16, b.r_pie.one_core,   b.has_pie(psram_ok),   b.unit);
  fmt_val(s[1], 16, b.r_nopie.one_core, b.has_nopie(psram_ok), b.unit);
  fmt_val(s[2], 16, b.r_pie.two_core,   b.has_pie(psram_ok),   b.unit);
  fmt_val(s[3], 16, b.r_nopie.two_core, b.has_nopie(psram_ok), b.unit);
  if (b.has_ratio(psram_ok))
    Serial.printf("%-16s %s %s %s %s %6.1fx  %s\n", b.name, s[0], s[1], s[2], s[3], b.ratio(), bench::unit_str(b.unit));
  else
    Serial.printf("%-16s %s %s %s %s %7s  %s\n", b.name, s[0], s[1], s[2], s[3], "-", bench::unit_str(b.unit));
}

void footer(bool psram_ok) {
  const bench::Peak p = bench::peak_of(true, psram_ok);
  const bench::Peak n = bench::peak_of(false, psram_ok);
  const bench::Peak dev = bench::peak_device(psram_ok);
  if (HAS_PIE) Serial.printf("peak PIE     = %.2f GOPS = %.4f TOPS (2 cores, %s)\n", p.gops, p.tops(), p.test);
  else         Serial.printf("peak PIE     = n/a (%s has no PIE)\n", ESP.getChipModel());
  Serial.printf("peak noPIE   = %.2f GOPS = %.4f TOPS (2 cores, %s)\n", n.gops, n.tops(), n.test);
  Serial.printf("DEVICE PEAK  = %.2f GOPS = %.4f TOPS (%s %s, 2 cores)\n", dev.gops, dev.tops(), dev.kind, dev.test);

  // 機械可読な CSV (tools/get_result.py が === CSV === 〜 === END === を取り込む)
  Serial.println("=== CSV ===");
  Serial.println("chip,mhz,test,pie_1core,nopie_1core,pie_2core,nopie_2core,unit");
  for (int i = 0; i < bench::count(); ++i) {
    const bench::Item& b = bench::items()[i];
    char v[4][16];
    csv_val(v[0], 16, b.r_pie.one_core,   b.has_pie(psram_ok));
    csv_val(v[1], 16, b.r_nopie.one_core, b.has_nopie(psram_ok));
    csv_val(v[2], 16, b.r_pie.two_core,   b.has_pie(psram_ok));
    csv_val(v[3], 16, b.r_nopie.two_core, b.has_nopie(psram_ok));
    Serial.printf("%s,%lu,%s,%s,%s,%s,%s,%s\n", ESP.getChipModel(), (unsigned long)getCpuFrequencyMhz(),
                  b.name, v[0], v[1], v[2], v[3], bench::unit_str(b.unit));
  }
  Serial.println("=== END ===");
}

}  // namespace report
