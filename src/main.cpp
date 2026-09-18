// M5Stack-BenchMark: ESP32 / ESP32-S3 / ESP32-P4 TOPS ベンチマーク — PIE あり / なし 比較
//   PIE SIMD 命令 (ee.vmulas 等) と通常の C スカラーコードで同じ処理を行い、
//   スループット (GOPS / TOPS) と倍率を M5Unified の Display とシリアルに表示する。
//   画面: 1 コアでの PIE / noPIE / 倍率。シリアル: 2 コア同時実行の合計も出力。

#include <M5Unified.h>
#include <esp_timer.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <string.h>
#include "bench.h"

// ---------------- バッファ設定 ----------------
static const size_t SRAM_SIZE  = 16 * 1024;     // 内部 SRAM: sram_a (前半 = a, 後半 = b), sram_b (出力/コピー先)
static const size_t PSRAM_SIZE = 512 * 1024;    // PSRAM:     psram_a (前半 = a, 後半 = b), psram_b
static const size_t SRAM_HALF  = SRAM_SIZE / 2;
static const size_t PSRAM_HALF = PSRAM_SIZE / 2;

struct Ctx {
  uint8_t* sram_a;   // SRAM_SIZE + 16  (dot の 1 ベクトル先読み用余白)
  uint8_t* sram_b;   // SRAM_SIZE
  uint8_t* psram_a;  // PSRAM_SIZE + 16
  uint8_t* psram_b;  // PSRAM_SIZE
};
static Ctx g_ctx[2];   // [0] = メインコア, [1] = ワーカーコア

// ---------------- カーネルラッパ (iters 回実行, 戻り値はチェックサム) ----------------
typedef uint32_t (*kernel_fn)(uint32_t iters, Ctx* c);
#define BARRIER() asm volatile("" ::: "memory")

#if HAS_PIE
// PIE レジスタピーク
static uint32_t p_s8_reg (uint32_t it, Ctx*) { return pie_bench_s8_accx(it); }
static uint32_t p_s16_reg(uint32_t it, Ctx*) { return pie_bench_s16_accx(it); }

// SRAM 8KB + 8KB の内積
static uint32_t p_dot_s8   (uint32_t it, Ctx* c) { return pie_dot_s8 (it, c->sram_a, c->sram_a + SRAM_HALF, SRAM_HALF / 16); }
static uint32_t p_dot_s16  (uint32_t it, Ctx* c) { return pie_dot_s16(it, c->sram_a, c->sram_a + SRAM_HALF, SRAM_HALF / 16); }
static uint32_t p_dot_s8_ps(uint32_t it, Ctx* c) { return pie_dot_s8 (it, c->psram_a, c->psram_a + PSRAM_HALF, PSRAM_HALF / 16); }
#endif
static uint32_t c_dot_s8_w (uint32_t it, Ctx* c) { int32_t s = 0; for (uint32_t i = 0; i < it; ++i) { s += c_dot_s8 ((int8_t*)c->sram_a, (int8_t*)(c->sram_a + SRAM_HALF), SRAM_HALF); BARRIER(); } return s; }
static uint32_t c_dot_s16_w(uint32_t it, Ctx* c) { int32_t s = 0; for (uint32_t i = 0; i < it; ++i) { s += c_dot_s16((int16_t*)c->sram_a, (int16_t*)(c->sram_a + SRAM_HALF), SRAM_HALF / 2); BARRIER(); } return s; }
static uint32_t c_dot_s8_ps(uint32_t it, Ctx* c) { int32_t s = 0; for (uint32_t i = 0; i < it; ++i) { s += c_dot_s8 ((int8_t*)c->psram_a, (int8_t*)(c->psram_a + PSRAM_HALF), PSRAM_HALF); BARRIER(); } return s; }

// SRAM 上の要素毎演算 out = a (op) b
#if HAS_PIE
#define MEM_OP_WRAP_PIE(name, pie_fn)                                                             \
  static uint32_t p_##name(uint32_t it, Ctx* c) {                                                \
    for (uint32_t i = 0; i < it; ++i) { pie_fn(c->sram_a, c->sram_a + SRAM_HALF, c->sram_b, SRAM_HALF / 16); BARRIER(); } \
    return c->sram_b[0]; }
#else
#define MEM_OP_WRAP_PIE(name, pie_fn)
#endif
#define MEM_OP_WRAP(name, pie_fn, c_fn, T)                                                        \
  MEM_OP_WRAP_PIE(name, pie_fn)                                                                  \
  static uint32_t c_##name(uint32_t it, Ctx* c) {                                                \
    for (uint32_t i = 0; i < it; ++i) { c_fn((T*)c->sram_a, (T*)(c->sram_a + SRAM_HALF), (T*)c->sram_b, SRAM_HALF / sizeof(T)); BARRIER(); } \
    return c->sram_b[0]; }
MEM_OP_WRAP(mul_s8,  pie_mul_s8,  c_mul_s8,  int8_t)
MEM_OP_WRAP(mul_s16, pie_mul_s16, c_mul_s16, int16_t)
MEM_OP_WRAP(add_s8,  pie_add_s8,  c_add_s8,  int8_t)
MEM_OP_WRAP(add_s16, pie_add_s16, c_add_s16, int16_t)
MEM_OP_WRAP(add_s32, pie_add_s32, c_add_s32, int32_t)

// スカラー (noPIE 列として表示)
static uint32_t s_int32_mac(uint32_t it, Ctx*) { return pie_bench_int32_mac(it); }
static uint32_t s_fp32_madd(uint32_t it, Ctx*) { return pie_bench_fp32_madd(it); }

// コピー: PIE = ee.vld/ee.vst, noPIE = memcpy
static const uint32_t NCHUNK = PSRAM_SIZE / SRAM_SIZE;
#if HAS_PIE
static uint32_t p_copy_sram (uint32_t it, Ctx* c) { for (uint32_t i = 0; i < it; ++i) { pie_copy(c->sram_b, c->sram_a, SRAM_SIZE / 16); BARRIER(); } return c->sram_b[0]; }
static uint32_t p_copy_s2p  (uint32_t it, Ctx* c) { for (uint32_t i = 0; i < it; ++i) { pie_copy(c->psram_b + (i % NCHUNK) * SRAM_SIZE, c->sram_a, SRAM_SIZE / 16); BARRIER(); } return c->psram_b[0]; }
static uint32_t p_copy_p2s  (uint32_t it, Ctx* c) { for (uint32_t i = 0; i < it; ++i) { pie_copy(c->sram_b, c->psram_a + (i % NCHUNK) * SRAM_SIZE, SRAM_SIZE / 16); BARRIER(); } return c->sram_b[0]; }
static uint32_t p_copy_psram(uint32_t it, Ctx* c) { for (uint32_t i = 0; i < it; ++i) { pie_copy(c->psram_b, c->psram_a, PSRAM_SIZE / 16); BARRIER(); } return c->psram_b[0]; }
#endif
static uint32_t c_copy_sram (uint32_t it, Ctx* c) { for (uint32_t i = 0; i < it; ++i) { memcpy(c->sram_b, c->sram_a, SRAM_SIZE); BARRIER(); } return c->sram_b[0]; }
static uint32_t c_copy_s2p  (uint32_t it, Ctx* c) { for (uint32_t i = 0; i < it; ++i) { memcpy(c->psram_b + (i % NCHUNK) * SRAM_SIZE, c->sram_a, SRAM_SIZE); BARRIER(); } return c->psram_b[0]; }
static uint32_t c_copy_p2s  (uint32_t it, Ctx* c) { for (uint32_t i = 0; i < it; ++i) { memcpy(c->sram_b, c->psram_a + (i % NCHUNK) * SRAM_SIZE, SRAM_SIZE); BARRIER(); } return c->sram_b[0]; }
static uint32_t c_copy_psram(uint32_t it, Ctx* c) { for (uint32_t i = 0; i < it; ++i) { memcpy(c->psram_b, c->psram_a, PSRAM_SIZE); BARRIER(); } return c->psram_b[0]; }

// ---------------- ベンチ定義 ----------------
enum Unit { U_GOPS, U_GFLOPS, U_MBPS };

struct Bench {
  const char* name;
  kernel_fn   pie;             // nullptr = PIE 版なし
  kernel_fn   nopie;           // nullptr = noPIE 版なし
  double      work_per_iter;   // ops or bytes (両版で同じ)
  Unit        unit;
  bool        need_psram;      // PSRAM の無い機種ではスキップ
  double      pie1, pie2;      // 1 core / 2 core 合計
  double      nopie1, nopie2;
};

#if HAS_PIE
#define PIE(fn) fn
#else
#define PIE(fn) nullptr
#endif

static Bench g_bench[] = {
  { "int8-mac-reg",    PIE(p_s8_reg),     nullptr,      OPS_S8_MAC_REG,        U_GOPS,   false },
  { "int16-mac-reg",   PIE(p_s16_reg),    nullptr,      OPS_S16_MAC_REG,       U_GOPS,   false },
  { "int8-mac-sram",   PIE(p_dot_s8),     c_dot_s8_w,   2.0 * SRAM_HALF,       U_GOPS,   false },
  { "int16-mac-sram",  PIE(p_dot_s16),    c_dot_s16_w,  2.0 * SRAM_HALF / 2,   U_GOPS,   false },
  { "int8-mac-psram",  PIE(p_dot_s8_ps),  c_dot_s8_ps,  2.0 * PSRAM_HALF,      U_GOPS,   true  },
  { "int8-mul-sram",   PIE(p_mul_s8),     c_mul_s8,     (double)SRAM_HALF,     U_GOPS,   false },
  { "int16-mul-sram",  PIE(p_mul_s16),    c_mul_s16,    (double)SRAM_HALF / 2, U_GOPS,   false },
  { "int8-add-sram",   PIE(p_add_s8),     c_add_s8,     (double)SRAM_HALF,     U_GOPS,   false },
  { "int16-add-sram",  PIE(p_add_s16),    c_add_s16,    (double)SRAM_HALF / 2, U_GOPS,   false },
  { "int32-add-sram",  PIE(p_add_s32),    c_add_s32,    (double)SRAM_HALF / 4, U_GOPS,   false },
  { "int32-scalar",    nullptr,           s_int32_mac,  OPS_INT32_MAC,         U_GOPS,   false },
  { "fp32-scalar",     nullptr,           s_fp32_madd,  OPS_FP32_MADD,         U_GFLOPS, false },
  { "copy-sram",       PIE(p_copy_sram),  c_copy_sram,  (double)SRAM_SIZE,     U_MBPS,   false },
  { "copy-sram>psram", PIE(p_copy_s2p),   c_copy_s2p,   (double)SRAM_SIZE,     U_MBPS,   true  },
  { "copy-psram>sram", PIE(p_copy_p2s),   c_copy_p2s,   (double)SRAM_SIZE,     U_MBPS,   true  },
  { "copy-psram",      PIE(p_copy_psram), c_copy_psram, (double)PSRAM_SIZE,    U_MBPS,   true  },
};
static const int N_BENCH = sizeof(g_bench) / sizeof(g_bench[0]);
static bool g_has_psram = false;

// PIE 版 / noPIE 版が実行可能か (機種と PSRAM の有無で決まる)
static bool has_pie(const Bench& b)   { return b.pie   && (!b.need_psram || g_has_psram); }
static bool has_nopie(const Bench& b) { return b.nopie && (!b.need_psram || g_has_psram); }
static const char* unit_str(Unit u) {
  switch (u) { case U_GOPS: return "GOPS"; case U_GFLOPS: return "GFLOPS"; default: return "MB/s"; }
}

// ---------------- 計測 ----------------
static const int64_t TARGET_US = 100000;   // 1 回の計測時間 (約)
static const int     N_RUNS    = 2;        // 最良値を採用

struct Job {
  kernel_fn fn;
  uint32_t  iters;
  Ctx*      ctx;
  int64_t   elapsed_us;
  uint32_t  checksum;
};

static void run_job(Job* j) {
  int64_t t0 = esp_timer_get_time();
  j->checksum = j->fn(j->iters, j->ctx);
  j->elapsed_us = esp_timer_get_time() - t0;
}

// ワーカーコア (メインの反対側) 用タスク
static SemaphoreHandle_t g_start_sem, g_done_sem;
static Job g_worker_job;

static void worker_task(void*) {
  for (;;) {
    xSemaphoreTake(g_start_sem, portMAX_DELAY);
    run_job(&g_worker_job);
    xSemaphoreGive(g_done_sem);
  }
}

// ~TARGET_US になるイテレーション数を求める
static uint32_t calibrate(kernel_fn fn, Ctx* ctx) {
  Job j = { fn, 1, ctx, 0, 0 };
  for (;;) {
    run_job(&j);
    if (j.elapsed_us >= 2000 || j.iters >= (1u << 30)) break;
    j.iters *= 2;
  }
  double it = (double)j.iters * (double)TARGET_US / (double)j.elapsed_us;
  if (it < 1) it = 1;
  if (it > 2e9) it = 2e9;
  return (uint32_t)it;
}

static double to_unit(double work, int64_t us, Unit u) {
  double per_sec = work / ((double)us * 1e-6);
  return (u == U_MBPS) ? per_sec / 1e6 : per_sec / 1e9;
}

// 1 コア / 2 コア同時 (各コアの実測スループットの合計) の最良値
static void measure(kernel_fn fn, double work, Unit unit, double* r1, double* r2) {
  uint32_t iters = calibrate(fn, &g_ctx[0]);
  double best1 = 0, best2 = 0;
  for (int r = 0; r < N_RUNS; ++r) {
    Job j = { fn, iters, &g_ctx[0], 0, 0 };
    run_job(&j);
    double v = to_unit(work * iters, j.elapsed_us, unit);
    if (v > best1) best1 = v;
    vTaskDelay(1);
  }
  for (int r = 0; r < N_RUNS; ++r) {
    g_worker_job = { fn, iters, &g_ctx[1], 0, 0 };
    Job j = { fn, iters, &g_ctx[0], 0, 0 };
    xSemaphoreGive(g_start_sem);
    run_job(&j);
    xSemaphoreTake(g_done_sem, portMAX_DELAY);
    double v = to_unit(work * iters, j.elapsed_us, unit)
             + to_unit(work * iters, g_worker_job.elapsed_us, unit);
    if (v > best2) best2 = v;
    vTaskDelay(1);
  }
  *r1 = best1;
  *r2 = best2;
}

// ---------------- 表示 ----------------
// 320x240 を基準にしたレイアウト。大きい画面 (Tab5 1280x720 など) は整数倍に拡大する
static int g_scale = 1;
#define ROW_Y0 (44 * g_scale)   // 表の先頭 y
#define ROW_H  ( 8 * g_scale)   // Font0 の行高

static void fmt_val(char* buf, size_t n, double v, bool valid, Unit u) {
  if (!valid) snprintf(buf, n, "%8s", "-");
  else if (u == U_MBPS) snprintf(buf, n, "%8.1f", v);
  else snprintf(buf, n, "%8.3f", v);
}

static void draw_header() {
  auto& d = M5.Display;
  int sx = d.width() / 320, sy = d.height() / 240;
  g_scale = (sx < sy ? sx : sy);
  if (g_scale < 1) g_scale = 1;
  d.setTextSize(g_scale);
  d.fillScreen(TFT_BLACK);
  d.setTextColor(TFT_WHITE, TFT_BLACK);
  d.setFont(&fonts::Font2);
  d.setCursor(0, 0);
  d.printf("%s rev%d %luMHz %s", ESP.getChipModel(), ESP.getChipRevision(),
           (unsigned long)getCpuFrequencyMhz(), HAS_PIE ? "PIE vs noPIE" : "(no PIE)");
  d.setCursor(0, 16 * g_scale);
  d.printf("1 core, PSRAM %luMB, MAC = 2 ops", (unsigned long)(ESP.getPsramSize() >> 20));
  d.setFont(&fonts::Font0);
  d.setTextColor(TFT_YELLOW, TFT_BLACK);
  d.setCursor(0, 34 * g_scale);
  d.printf("%-15s %8s %8s %7s %s", "test", "PIE", "noPIE", "ratio", "unit");
}

static void draw_row(int i, int state) {   // state: 0 = 未実行, 1 = 実行中, 2 = 完了
  auto& d = M5.Display;
  Bench& b = g_bench[i];
  int y = ROW_Y0 + i * ROW_H;
  d.setFont(&fonts::Font0);
  d.fillRect(0, y, d.width(), ROW_H, TFT_BLACK);
  d.setCursor(0, y);
  if (state == 1) {
    d.setTextColor(TFT_CYAN, TFT_BLACK);
    d.printf("%-15s %8s", b.name, "...");
  } else if (state == 2) {
    char sp[16], sn[16], sr[16];
    fmt_val(sp, sizeof sp, b.pie1, has_pie(b), b.unit);
    fmt_val(sn, sizeof sn, b.nopie1, has_nopie(b), b.unit);
    if (has_pie(b) && has_nopie(b) && b.nopie1 > 0) snprintf(sr, sizeof sr, "%6.1fx", b.pie1 / b.nopie1);
    else snprintf(sr, sizeof sr, "%7s", "-");
    d.setTextColor(TFT_WHITE, TFT_BLACK);
    d.printf("%-15s %s %s %s %s", b.name, sp, sn, sr, unit_str(b.unit));
  } else {
    d.setTextColor(TFT_DARKGREY, TFT_BLACK);
    d.printf("%-15s", b.name);
  }
}

// GOPS 行の最大値 (2 コア合計) と、それを出した項目名
struct Peak { double gops; const char* test; const char* kind; };
static Peak peak_of(bool pie_side) {
  Peak pk = { 0, "-", pie_side ? "PIE" : "noPIE" };
  for (int i = 0; i < N_BENCH; ++i) {
    const Bench& b = g_bench[i];
    if (b.unit != U_GOPS) continue;
    if (pie_side  && has_pie(b)   && b.pie2   > pk.gops) { pk.gops = b.pie2;   pk.test = b.name; }
    if (!pie_side && has_nopie(b) && b.nopie2 > pk.gops) { pk.gops = b.nopie2; pk.test = b.name; }
  }
  return pk;
}
static Peak peak_device() {   // チップ全体のピーク = PIE / noPIE の大きい方
  Peak p = peak_of(true), n = peak_of(false);
  return (p.gops >= n.gops) ? p : n;
}

static void draw_summary() {
  auto& d = M5.Display;
  Peak dev = peak_device(), n = peak_of(false);
  int y = ROW_Y0 + N_BENCH * ROW_H + 4 * g_scale;
  d.fillRect(0, y, d.width(), d.height() - y, TFT_BLACK);
  d.setFont(&fonts::Font2);
  d.setTextColor(TFT_GREEN, TFT_BLACK);
  d.setCursor(0, y);
  d.printf("PEAK %.2f GOPS = %.4f TOPS", dev.gops, dev.gops / 1000.0);
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

// ---------------- 実行 ----------------
static void fill_pattern(uint8_t* p, size_t n, uint32_t seed) {
  uint32_t x = seed;
  for (size_t i = 0; i < n; ++i) {
    x = x * 1664525u + 1013904223u;
    p[i] = (uint8_t)(x >> 24);
  }
}

static bool alloc_ctx(Ctx& c, uint32_t seed) {
  c.sram_a  = (uint8_t*)heap_caps_aligned_alloc(16, SRAM_SIZE + 16,  MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  c.sram_b  = (uint8_t*)heap_caps_aligned_alloc(16, SRAM_SIZE,       MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!c.sram_a || !c.sram_b) return false;
  fill_pattern(c.sram_a, SRAM_SIZE + 16, seed);
  memset(c.sram_b, 0, SRAM_SIZE);
  c.psram_a = c.psram_b = nullptr;
  if (psramFound()) {
    c.psram_a = (uint8_t*)heap_caps_aligned_alloc(16, PSRAM_SIZE + 16, MALLOC_CAP_SPIRAM);
    c.psram_b = (uint8_t*)heap_caps_aligned_alloc(16, PSRAM_SIZE,      MALLOC_CAP_SPIRAM);
    if (c.psram_a && c.psram_b) {
      fill_pattern(c.psram_a, PSRAM_SIZE + 16, seed ^ 0x5a5a);
      memset(c.psram_b, 0, PSRAM_SIZE);
    } else {
      c.psram_a = c.psram_b = nullptr;
    }
  }
  return true;
}

static void csv_val(char* buf, double v, bool valid) {
  if (valid) snprintf(buf, 16, "%.4f", v); else buf[0] = 0;
}

static void run_all() {
  draw_header();
  for (int i = 0; i < N_BENCH; ++i) draw_row(i, 0);

  Serial.printf("\ndevice       = %s rev%d, %lu MHz, 2 cores, PSRAM %lu MB\n",
                ESP.getChipModel(), ESP.getChipRevision(), (unsigned long)getCpuFrequencyMhz(),
                (unsigned long)(ESP.getPsramSize() >> 20));
  Serial.printf("%-16s %9s %9s %9s %9s %7s  unit\n", "", "PIE-1c", "noPIE-1c", "PIE-2c", "noPIE-2c", "ratio");

  for (int i = 0; i < N_BENCH; ++i) {
    Bench& b = g_bench[i];
    draw_row(i, 1);
    if (has_pie(b))   measure(b.pie,   b.work_per_iter, b.unit, &b.pie1,   &b.pie2);
    if (has_nopie(b)) measure(b.nopie, b.work_per_iter, b.unit, &b.nopie1, &b.nopie2);
    draw_row(i, 2);
    char s[4][16];
    fmt_val(s[0], 16, b.pie1,   has_pie(b),   b.unit);
    fmt_val(s[1], 16, b.nopie1, has_nopie(b), b.unit);
    fmt_val(s[2], 16, b.pie2,   has_pie(b),   b.unit);
    fmt_val(s[3], 16, b.nopie2, has_nopie(b), b.unit);
    if (has_pie(b) && has_nopie(b) && b.nopie1 > 0)
      Serial.printf("%-16s %9s %9s %9s %9s %6.1fx  %s\n", b.name, s[0], s[1], s[2], s[3], b.pie1 / b.nopie1, unit_str(b.unit));
    else
      Serial.printf("%-16s %9s %9s %9s %9s %7s  %s\n", b.name, s[0], s[1], s[2], s[3], "-", unit_str(b.unit));
  }
  draw_summary();

  Peak p = peak_of(true), n = peak_of(false), dev = peak_device();
  if (HAS_PIE) Serial.printf("peak PIE     = %.2f GOPS = %.4f TOPS (2 cores, %s)\n", p.gops, p.gops / 1000.0, p.test);
  else         Serial.printf("peak PIE     = n/a (%s has no PIE)\n", ESP.getChipModel());
  Serial.printf("peak noPIE   = %.2f GOPS = %.4f TOPS (2 cores, %s)\n", n.gops, n.gops / 1000.0, n.test);
  Serial.printf("DEVICE PEAK  = %.2f GOPS = %.4f TOPS (%s %s, 2 cores)\n", dev.gops, dev.gops / 1000.0, dev.kind, dev.test);

  // 機械可読な CSV (tools/get_result.py が === CSV === ～ === END === を取り込む)
  Serial.println("=== CSV ===");
  Serial.println("chip,mhz,test,pie_1core,nopie_1core,pie_2core,nopie_2core,unit");
  for (int i = 0; i < N_BENCH; ++i) {
    Bench& b = g_bench[i];
    char v[4][16];
    csv_val(v[0], b.pie1,   has_pie(b));
    csv_val(v[1], b.nopie1, has_nopie(b));
    csv_val(v[2], b.pie2,   has_pie(b));
    csv_val(v[3], b.nopie2, has_nopie(b));
    Serial.printf("%s,%lu,%s,%s,%s,%s,%s,%s\n", ESP.getChipModel(), (unsigned long)getCpuFrequencyMhz(),
                  b.name, v[0], v[1], v[2], v[3], unit_str(b.unit));
  }
  Serial.println("=== END ===");
  while (Serial.available()) Serial.read();   // 計測中に届いたコマンドは捨てる
}

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(115200);
  M5.Display.setRotation(1);
  M5.Display.setBrightness(128);

  if (!alloc_ctx(g_ctx[0], 1234) || !alloc_ctx(g_ctx[1], 5678)) {
    M5.Display.setFont(&fonts::Font2);
    M5.Display.println("buffer alloc failed");
    Serial.println("buffer alloc failed");
    for (;;) delay(1000);
  }

  g_has_psram = g_ctx[0].psram_a && g_ctx[1].psram_a;
  g_start_sem = xSemaphoreCreateBinary();
  g_done_sem  = xSemaphoreCreateBinary();
  int other_core = (xPortGetCoreID() == 0) ? 1 : 0;
  xTaskCreatePinnedToCore(worker_task, "bench_worker", 8192, nullptr, 5, nullptr, other_core);

  run_all();
}

void loop() {
  M5.update();
  bool rerun = M5.Touch.getDetail().wasClicked() || M5.BtnA.wasClicked() || M5.BtnB.wasClicked() || M5.BtnC.wasClicked();
  while (Serial.available()) {
    int ch = Serial.read();
    if (ch == 'r' || ch == 'R') rerun = true;   // PC から "r" を送ると再測定
  }
  if (rerun) run_all();
  delay(20);
}
