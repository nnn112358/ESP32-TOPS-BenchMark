// ESP32-TOPS-BenchMark — "PIE なし" 比較用カーネル: ごく普通の C スカラーコード (-O2)
// Copyright (c) 2026 nnn112358 <https://github.com/nnn112358>
// SPDX-License-Identifier: MIT
// Developed with Claude Code (Anthropic).
//   gcc は PIE 命令への自動ベクトル化を行わないため、SIMD を書かない場合に得られる素の性能を表す。
#include "kernels.h"

#define NOINLINE __attribute__((noinline))

NOINLINE int32_t c_dot_s8(const int8_t* a, const int8_t* b, uint32_t n) {
  int32_t s0 = 0, s1 = 0, s2 = 0, s3 = 0;
  for (uint32_t i = 0; i < n; i += 4) {
    s0 += (int32_t)a[i]     * b[i];
    s1 += (int32_t)a[i + 1] * b[i + 1];
    s2 += (int32_t)a[i + 2] * b[i + 2];
    s3 += (int32_t)a[i + 3] * b[i + 3];
  }
  return s0 + s1 + s2 + s3;
}

NOINLINE int32_t c_dot_s16(const int16_t* a, const int16_t* b, uint32_t n) {
  int32_t s0 = 0, s1 = 0, s2 = 0, s3 = 0;
  for (uint32_t i = 0; i < n; i += 4) {
    s0 += (int32_t)a[i]     * b[i];
    s1 += (int32_t)a[i + 1] * b[i + 1];
    s2 += (int32_t)a[i + 2] * b[i + 2];
    s3 += (int32_t)a[i + 3] * b[i + 3];
  }
  return s0 + s1 + s2 + s3;
}

NOINLINE void c_mul_s8(const int8_t* a, const int8_t* b, int8_t* out, uint32_t n) {
  for (uint32_t i = 0; i < n; ++i) out[i] = (int8_t)((int32_t)a[i] * b[i]);
}

NOINLINE void c_mul_s16(const int16_t* a, const int16_t* b, int16_t* out, uint32_t n) {
  for (uint32_t i = 0; i < n; ++i) out[i] = (int16_t)((int32_t)a[i] * b[i]);
}

NOINLINE void c_add_s8(const int8_t* a, const int8_t* b, int8_t* out, uint32_t n) {
  for (uint32_t i = 0; i < n; ++i) {
    int32_t v = (int32_t)a[i] + b[i];
    out[i] = (int8_t)(v > 127 ? 127 : (v < -128 ? -128 : v));
  }
}

NOINLINE void c_add_s16(const int16_t* a, const int16_t* b, int16_t* out, uint32_t n) {
  for (uint32_t i = 0; i < n; ++i) {
    int32_t v = (int32_t)a[i] + b[i];
    out[i] = (int16_t)(v > 32767 ? 32767 : (v < -32768 ? -32768 : v));
  }
}

NOINLINE void c_add_s32(const int32_t* a, const int32_t* b, int32_t* out, uint32_t n) {
  for (uint32_t i = 0; i < n; ++i) {
    int64_t v = (int64_t)a[i] + b[i];
    out[i] = (int32_t)(v > INT32_MAX ? INT32_MAX : (v < INT32_MIN ? INT32_MIN : v));
  }
}
