// M5Stack-BenchMark — 計測 (キャリブレーション、1 コア / 2 コア同時実行)
// Copyright (c) 2026 nnn112358 <https://github.com/nnn112358>
// SPDX-License-Identifier: MIT
// Developed with Claude Code (Anthropic).
#include "measure.h"
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

namespace measure {

namespace {

constexpr int64_t  TARGET_US    = 100000;      // 1 回の計測時間 (約)
constexpr int64_t  MIN_CALIB_US = 2000;        // キャリブレーションで信用する最小の経過時間
constexpr int      N_RUNS       = 2;           // 最良値を採用
constexpr uint32_t MAX_ITERS    = 2000000000u;

struct Job {
  bench::KernelFn fn;
  uint32_t        iters;
  buffers::Set*   buf;
  int64_t         elapsed_us;
  uint32_t        checksum;
};

buffers::Set*     g_buf;
SemaphoreHandle_t g_start_sem, g_done_sem;
Job               g_worker_job;

void run_job(Job& j) {
  const int64_t t0 = esp_timer_get_time();
  j.checksum = j.fn(j.iters, j.buf);
  j.elapsed_us = esp_timer_get_time() - t0;
}

// ワーカーコアはセマフォで起こされるまで待ち、1 ジョブ実行して完了を通知する
void worker_task(void*) {
  for (;;) {
    xSemaphoreTake(g_start_sem, portMAX_DELAY);
    run_job(g_worker_job);
    xSemaphoreGive(g_done_sem);
  }
}

// 1 イテレーションでは短すぎるので、~TARGET_US になるイテレーション数を求める
uint32_t calibrate(bench::KernelFn fn) {
  Job j = { fn, 1, &g_buf[0], 0, 0 };
  for (;;) {
    run_job(j);
    if (j.elapsed_us >= MIN_CALIB_US || j.iters >= (1u << 30)) break;
    j.iters *= 2;
  }
  double it = (double)j.iters * (double)TARGET_US / (double)j.elapsed_us;
  if (it < 1) it = 1;
  if (it > MAX_ITERS) it = MAX_ITERS;
  return (uint32_t)it;
}

double to_unit(double work, int64_t us, bench::Unit u) {
  const double per_sec = work / ((double)us * 1e-6);
  return (u == bench::Unit::MBPS) ? per_sec / 1e6 : per_sec / 1e9;
}

}  // namespace

void init(buffers::Set* buf) {
  g_buf = buf;
  g_start_sem = xSemaphoreCreateBinary();
  g_done_sem  = xSemaphoreCreateBinary();
  const int other_core = (xPortGetCoreID() == 0) ? 1 : 0;
  xTaskCreatePinnedToCore(worker_task, "bench_worker", 8192, nullptr, 5, nullptr, other_core);
}

bench::Result run(bench::KernelFn fn, double work_per_iter, bench::Unit unit) {
  const uint32_t iters = calibrate(fn);
  const double   work  = work_per_iter * iters;
  bench::Result  r;

  for (int n = 0; n < N_RUNS; ++n) {
    Job j = { fn, iters, &g_buf[0], 0, 0 };
    run_job(j);
    const double v = to_unit(work, j.elapsed_us, unit);
    if (v > r.one_core) r.one_core = v;
    vTaskDelay(1);
  }
  for (int n = 0; n < N_RUNS; ++n) {
    g_worker_job = { fn, iters, &g_buf[1], 0, 0 };
    Job j = { fn, iters, &g_buf[0], 0, 0 };
    xSemaphoreGive(g_start_sem);
    run_job(j);
    xSemaphoreTake(g_done_sem, portMAX_DELAY);
    const double v = to_unit(work, j.elapsed_us, unit) + to_unit(work, g_worker_job.elapsed_us, unit);
    if (v > r.two_core) r.two_core = v;
    vTaskDelay(1);
  }
  return r;
}

}  // namespace measure
