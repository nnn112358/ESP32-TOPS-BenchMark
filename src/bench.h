// ESP32-S3 ベンチマークカーネルのプロトタイプ
//   pie_*  : PIE SIMD 命令 (pie_kernels.S)   … "PIE あり"
//   c_*    : 通常の C スカラーコード (c_kernels.cpp) … "PIE なし"
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "sdkconfig.h"

#if CONFIG_IDF_TARGET_ESP32S3
#define HAS_PIE 1
#else
#define HAS_PIE 0      // 無印 ESP32 (LX6) には PIE がない
#endif

#ifdef __cplusplus
extern "C" {
#endif

#if HAS_PIE
// ---- PIE: レジスタ演算のみ (メモリアクセスなし) : 演算ピーク ----
uint32_t pie_bench_s8_accx (uint32_t iters);   // ee.vmulas.s8.accx  16 lane MAC x16 / iter
uint32_t pie_bench_s16_accx(uint32_t iters);   // ee.vmulas.s16.accx  8 lane MAC x16 / iter

// ---- PIE: メモリ上のデータに対する処理 (nvec = 16 バイトベクトル数) ----
// バッファはすべて 16 バイト境界。dot は a, b とも (nvec + 1) * 16 バイト以上必要 (1 ベクトル先読み)
uint32_t pie_dot_s8 (uint32_t iters, const void* a, const void* b, uint32_t nvec);  // acc += a[i]*b[i]
uint32_t pie_dot_s16(uint32_t iters, const void* a, const void* b, uint32_t nvec);
void pie_mul_s8  (const void* a, const void* b, void* out, uint32_t nvec);  // out[i] = a[i]*b[i]  (SAR=0)
void pie_mul_s16 (const void* a, const void* b, void* out, uint32_t nvec);
void pie_add_s8  (const void* a, const void* b, void* out, uint32_t nvec);  // out[i] = sat(a[i]+b[i])
void pie_add_s16 (const void* a, const void* b, void* out, uint32_t nvec);
void pie_add_s32 (const void* a, const void* b, void* out, uint32_t nvec);
void pie_copy    (void* dst, const void* src, uint32_t nvec);               // ee.vld.128 / ee.vst.128

#endif // HAS_PIE
#define OPS_S8_MAC_REG   (16 * 2 * 16)
#define OPS_S16_MAC_REG  ( 8 * 2 * 16)

// ---- スカラー (アセンブリ, 比較用; ESP32 / ESP32-S3 共通) ----
uint32_t pie_bench_int32_mac(uint32_t iters);  // mull + add  8 MAC / iter
uint32_t pie_bench_fp32_madd(uint32_t iters);  // madd.s     16 FMA / iter
#define OPS_INT32_MAC ( 8 * 2)
#define OPS_FP32_MADD (16 * 2)

// ---- noPIE: C スカラー (n = 要素数) ----
int32_t c_dot_s8 (const int8_t*  a, const int8_t*  b, uint32_t n);
int32_t c_dot_s16(const int16_t* a, const int16_t* b, uint32_t n);
void c_mul_s8 (const int8_t*  a, const int8_t*  b, int8_t*  out, uint32_t n);
void c_mul_s16(const int16_t* a, const int16_t* b, int16_t* out, uint32_t n);
void c_add_s8 (const int8_t*  a, const int8_t*  b, int8_t*  out, uint32_t n);
void c_add_s16(const int16_t* a, const int16_t* b, int16_t* out, uint32_t n);
void c_add_s32(const int32_t* a, const int32_t* b, int32_t* out, uint32_t n);

#ifdef __cplusplus
}
#endif
