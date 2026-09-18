// ESP32-TOPS-BenchMark — 測定項目の定義 (PIE 版 / noPIE 版カーネルのペア)
// Copyright (c) 2026 nnn112358 <https://github.com/nnn112358>
// SPDX-License-Identifier: MIT
// Developed with Claude Code (Anthropic).
#include "benchmarks.h"
#include <string.h>
#include "kernels.h"

namespace bench {

using buffers::Set;
using namespace buffers;

namespace {

// コンパイラがループを消さないようにする
inline void barrier() { asm volatile("" ::: "memory"); }

// ---- 入力の置き場所 ----
enum class Mem { SRAM, PSRAM };

template <Mem M> const uint8_t* in_a(Set* b);
template <Mem M> const uint8_t* in_b(Set* b);
template <Mem M> size_t         in_bytes();
template <> const uint8_t* in_a<Mem::SRAM>(Set* b)  { return b->sram_a; }
template <> const uint8_t* in_b<Mem::SRAM>(Set* b)  { return b->sram_a + SRAM_HALF; }
template <> size_t         in_bytes<Mem::SRAM>()    { return SRAM_HALF; }
template <> const uint8_t* in_a<Mem::PSRAM>(Set* b) { return b->psram_a; }
template <> const uint8_t* in_b<Mem::PSRAM>(Set* b) { return b->psram_a + PSRAM_HALF; }
template <> size_t         in_bytes<Mem::PSRAM>()   { return PSRAM_HALF; }

// ---- 内積: acc += a[i] * b[i] ----
using PieDotFn = uint32_t (*)(uint32_t, const void*, const void*, uint32_t);
template <typename T> using CDotFn = int32_t (*)(const T*, const T*, uint32_t);

template <PieDotFn Fn, Mem M>
uint32_t pie_dot(uint32_t it, Set* b) {
  return Fn(it, in_a<M>(b), in_b<M>(b), in_bytes<M>() / VEC);
}
template <typename T, CDotFn<T> Fn, Mem M>
uint32_t c_dot(uint32_t it, Set* b) {
  int32_t s = 0;
  for (uint32_t i = 0; i < it; ++i) {
    s += Fn((const T*)in_a<M>(b), (const T*)in_b<M>(b), in_bytes<M>() / sizeof(T));
    barrier();
  }
  return (uint32_t)s;
}

// ---- 要素毎演算: out[i] = a[i] (op) b[i]  (SRAM 上、出力は sram_b) ----
using PieOp3Fn = void (*)(const void*, const void*, void*, uint32_t);
template <typename T> using COp3Fn = void (*)(const T*, const T*, T*, uint32_t);

template <PieOp3Fn Fn>
uint32_t pie_op3(uint32_t it, Set* b) {
  for (uint32_t i = 0; i < it; ++i) {
    Fn(in_a<Mem::SRAM>(b), in_b<Mem::SRAM>(b), b->sram_b, SRAM_HALF / VEC);
    barrier();
  }
  return b->sram_b[0];
}
template <typename T, COp3Fn<T> Fn>
uint32_t c_op3(uint32_t it, Set* b) {
  for (uint32_t i = 0; i < it; ++i) {
    Fn((const T*)in_a<Mem::SRAM>(b), (const T*)in_b<Mem::SRAM>(b), (T*)b->sram_b, SRAM_HALF / sizeof(T));
    barrier();
  }
  return b->sram_b[0];
}

// ---- レジスタのみ / スカラー (バッファ不要) ----
using PlainFn = uint32_t (*)(uint32_t);
template <PlainFn Fn>
uint32_t plain(uint32_t it, Set*) { return Fn(it); }

// ---- コピー ----
enum class Copy { SRAM_SRAM, SRAM_PSRAM, PSRAM_SRAM, PSRAM_PSRAM };
using CopyFn = void (*)(void* dst, const void* src, size_t bytes);

void copy_memcpy(void* dst, const void* src, size_t bytes) { memcpy(dst, src, bytes); }
#if HAS_PIE
void copy_pie(void* dst, const void* src, size_t bytes) { pie_copy(dst, src, bytes / VEC); }
#endif

// SRAM <-> PSRAM は 16 KB を PSRAM 上でローテーションしてキャッシュヒットを避ける
constexpr uint32_t NCHUNK = PSRAM_SIZE / SRAM_SIZE;

template <Copy C, CopyFn Fn>
uint32_t copy(uint32_t it, Set* b) {
  for (uint32_t i = 0; i < it; ++i) {
    const size_t off = (i % NCHUNK) * SRAM_SIZE;
    switch (C) {
      case Copy::SRAM_SRAM:   Fn(b->sram_b,        b->sram_a,        SRAM_SIZE);  break;
      case Copy::SRAM_PSRAM:  Fn(b->psram_b + off, b->sram_a,        SRAM_SIZE);  break;
      case Copy::PSRAM_SRAM:  Fn(b->sram_b,        b->psram_a + off, SRAM_SIZE);  break;
      case Copy::PSRAM_PSRAM: Fn(b->psram_b,       b->psram_a,       PSRAM_SIZE); break;
    }
    barrier();
  }
  return (C == Copy::SRAM_SRAM || C == Copy::PSRAM_SRAM) ? b->sram_b[0] : b->psram_b[0];
}

#if HAS_PIE
#define PIE_FN(expr) (expr)
#else
#define PIE_FN(expr) nullptr
#endif

constexpr double MAC = 2.0;   // MAC = 乗算 + 加算 = 2 ops

Item g_items[] = {
  // name               PIE 版                                                noPIE 版                                        work/iter                 unit          PSRAM
  { "int8-mac-reg",    PIE_FN(plain<pie_bench_s8_accx>),                     nullptr,                                        OPS_S8_MAC_REG,           Unit::GOPS,   false },
  { "int16-mac-reg",   PIE_FN(plain<pie_bench_s16_accx>),                    nullptr,                                        OPS_S16_MAC_REG,          Unit::GOPS,   false },
  { "int8-mac-sram",   PIE_FN((pie_dot<pie_dot_s8,  Mem::SRAM>)),            c_dot<int8_t,  c_dot_s8,  Mem::SRAM>,           MAC * SRAM_HALF,          Unit::GOPS,   false },
  { "int16-mac-sram",  PIE_FN((pie_dot<pie_dot_s16, Mem::SRAM>)),            c_dot<int16_t, c_dot_s16, Mem::SRAM>,           MAC * SRAM_HALF / 2,      Unit::GOPS,   false },
  { "int8-mac-psram",  PIE_FN((pie_dot<pie_dot_s8,  Mem::PSRAM>)),           c_dot<int8_t,  c_dot_s8,  Mem::PSRAM>,          MAC * PSRAM_HALF,         Unit::GOPS,   true  },
  { "int8-mul-sram",   PIE_FN(pie_op3<pie_mul_s8>),                          c_op3<int8_t,  c_mul_s8>,                       (double)SRAM_HALF,        Unit::GOPS,   false },
  { "int16-mul-sram",  PIE_FN(pie_op3<pie_mul_s16>),                         c_op3<int16_t, c_mul_s16>,                      (double)SRAM_HALF / 2,    Unit::GOPS,   false },
  { "int8-add-sram",   PIE_FN(pie_op3<pie_add_s8>),                          c_op3<int8_t,  c_add_s8>,                       (double)SRAM_HALF,        Unit::GOPS,   false },
  { "int16-add-sram",  PIE_FN(pie_op3<pie_add_s16>),                         c_op3<int16_t, c_add_s16>,                      (double)SRAM_HALF / 2,    Unit::GOPS,   false },
  { "int32-add-sram",  PIE_FN(pie_op3<pie_add_s32>),                         c_op3<int32_t, c_add_s32>,                      (double)SRAM_HALF / 4,    Unit::GOPS,   false },
  { "int32-scalar",    nullptr,                                              plain<pie_bench_int32_mac>,                     OPS_INT32_MAC,            Unit::GOPS,   false },
  { "fp32-scalar",     nullptr,                                              plain<pie_bench_fp32_madd>,                     OPS_FP32_MADD,            Unit::GFLOPS, false },
  { "copy-sram",       PIE_FN((copy<Copy::SRAM_SRAM,   copy_pie>)),          copy<Copy::SRAM_SRAM,   copy_memcpy>,           (double)SRAM_SIZE,        Unit::MBPS,   false },
  { "copy-sram>psram", PIE_FN((copy<Copy::SRAM_PSRAM,  copy_pie>)),          copy<Copy::SRAM_PSRAM,  copy_memcpy>,           (double)SRAM_SIZE,        Unit::MBPS,   true  },
  { "copy-psram>sram", PIE_FN((copy<Copy::PSRAM_SRAM,  copy_pie>)),          copy<Copy::PSRAM_SRAM,  copy_memcpy>,           (double)SRAM_SIZE,        Unit::MBPS,   true  },
  { "copy-psram",      PIE_FN((copy<Copy::PSRAM_PSRAM, copy_pie>)),          copy<Copy::PSRAM_PSRAM, copy_memcpy>,           (double)PSRAM_SIZE,       Unit::MBPS,   true  },
};

}  // namespace

const char* unit_str(Unit u) {
  switch (u) {
    case Unit::GOPS:   return "GOPS";
    case Unit::GFLOPS: return "GFLOPS";
    default:           return "MB/s";
  }
}

Item* items() { return g_items; }
int   count() { return (int)(sizeof(g_items) / sizeof(g_items[0])); }

Peak peak_of(bool pie_side, bool psram_ok) {
  Peak pk;
  pk.kind = pie_side ? "PIE" : "noPIE";
  for (int i = 0; i < count(); ++i) {
    const Item& b = g_items[i];
    if (b.unit != Unit::GOPS) continue;
    const bool ok = pie_side ? b.has_pie(psram_ok) : b.has_nopie(psram_ok);
    const double v = pie_side ? b.r_pie.two_core : b.r_nopie.two_core;
    if (ok && v > pk.gops) { pk.gops = v; pk.test = b.name; }
  }
  return pk;
}

Peak peak_device(bool psram_ok) {
  Peak p = peak_of(true, psram_ok), n = peak_of(false, psram_ok);
  return (p.gops >= n.gops) ? p : n;
}

}  // namespace bench
