/*
 * ESP32-TOPS-BenchMark — ESP32-P4 (RISC-V) アセンブリカーネル
 * Copyright (c) 2026 nnn112358 <https://github.com/nnn112358>
 * SPDX-License-Identifier: MIT
 * Developed with Claude Code (Anthropic).
 *
 * ESP32-P4 PIE (Processor Instruction Extensions) SIMD benchmark kernels (RISC-V)
 *
 * S3 版 (pie_kernels.S) と同じ関数名・引数で実装し、main.cpp から共通に呼べるようにする。
 * RISC-V ABI: a0..a7 = 引数, 戻り値 = a0, t0..t6 = caller-saved
 * PIE 命令で使える AR は a0-a5, s0-s1, s8-s11, t3-t6 のみ (t0-t2 は不可)
 * esp.lp.setup (ゼロオーバーヘッドループ) はラベルが本体の「最後の」命令、count は PIE 可 AR、0 は事前に除外
 * 圧縮命令があると lp.setup の終端アドレスが狂うため .option norvc
 *
 * 注意: lp.setup の回数レジスタは大きな値 (数千以上) では正しく回らなかった (実機 rev1.0 で確認)。
 *       そのため「外側ソフトウェアループ x 内側 lp.setup 256 回 + 端数ループ」の構造にしている。
 */
#include "sdkconfig.h"
#if CONFIG_IDF_TARGET_ESP32P4

    .text
    .option norvc

#define CHUNK 256

/* ---------- q0 = q1 = {0x01020304 x4} ---------- */
    .macro  INIT_Q01
    li      t3, 0x01020304
    esp.movi.32.q   q0, t3, 0
    esp.movi.32.q   q0, t3, 1
    esp.movi.32.q   q0, t3, 2
    esp.movi.32.q   q0, t3, 3
    esp.movi.32.q   q1, t3, 0
    esp.movi.32.q   q1, t3, 1
    esp.movi.32.q   q1, t3, 2
    esp.movi.32.q   q1, t3, 3
    .endm

/* ---------- 同じ命令を 15 回 + ラベル付き最後の 1 回 (lp.setup 用) ---------- */
    .macro  REP16_LP insn:vararg
    \insn
    \insn
    \insn
    \insn
    \insn
    \insn
    \insn
    \insn
    \insn
    \insn
    \insn
    \insn
    \insn
    \insn
    \insn
0:  \insn
    .endm

/*
 * 本体マクロ \body を合計 \cnt 回実行する (cnt, chunks, rem, c256 はすべて PIE 可 AR で、破壊される)
 *   \body は最後の命令に "0:" ラベルを持つこと
 */
    .macro  CHUNKED_LOOP cnt, chunks, rem, c256, body:vararg
    srli    \chunks, \cnt, 8
    andi    \rem, \cnt, CHUNK-1
    li      \c256, CHUNK
    beqz    \chunks, 2f
1:
    esp.lp.setup 0, \c256, 0f
    \body
    addi    \chunks, \chunks, -1
    bnez    \chunks, 1b
2:
    beqz    \rem, 3f
    esp.lp.setup 0, \rem, 0f
    \body
3:
    .endm

/* =====================================================================
 * レジスタ演算のみ: uint32_t pie_bench_s8_accx(uint32_t iters)   (P4 では XACC)
 * ===================================================================== */
    .global pie_bench_s8_accx
    .type   pie_bench_s8_accx, @function
    .balign 4
pie_bench_s8_accx:
    INIT_Q01
    esp.zero.xacc
    CHUNKED_LOOP a0, a1, a2, a3, REP16_LP esp.vmulas.s8.xacc q0, q1
    esp.movx.r.xacc.l a0
    ret

/* =====================================================================
 * レジスタ演算のみ: uint32_t pie_bench_s16_accx(uint32_t iters)
 * ===================================================================== */
    .global pie_bench_s16_accx
    .type   pie_bench_s16_accx, @function
    .balign 4
pie_bench_s16_accx:
    INIT_Q01
    esp.zero.xacc
    CHUNKED_LOOP a0, a1, a2, a3, REP16_LP esp.vmulas.s16.xacc q0, q1
    esp.movx.r.xacc.l a0
    ret

/* =====================================================================
 * 内積: uint32_t pie_dot_s8(uint32_t iters, const void* a, const void* b, uint32_t nvec)
 *   a0 = iters, a1 = a, a2 = b, a3 = nvec   (a, b とも (nvec+1)*16 バイト以上)
 *   内側ループ本体 2 命令 / 16 MAC: ロード+MAC 融合命令 + ロード
 * ===================================================================== */
    .macro  DOT_BODY mac_ld
    \mac_ld q0, a4, 16, q0, q1
0:  esp.vld.128.ip  q1, a5, 16
    .endm

    .macro  DOT_KERNEL name, mac_ld
    .global \name
    .type   \name, @function
    .balign 4
\name:
    esp.zero.qacc
5:
    mv      a4, a1
    mv      a5, a2
    esp.vld.128.ip  q0, a4, 16
    esp.vld.128.ip  q1, a5, 16
    mv      t3, a3
    CHUNKED_LOOP t3, t4, t5, t6, DOT_BODY \mac_ld
    addi    a0, a0, -1
    bnez    a0, 5b
    li      t3, 0
    esp.srcmb.s8.qacc q2, t3, 0
    esp.movi.32.a   q2, a0, 0
    ret
    .endm

    DOT_KERNEL pie_dot_s8,  esp.vmulas.s8.qacc.ld.ip
    DOT_KERNEL pie_dot_s16, esp.vmulas.s16.qacc.ld.ip

/* =====================================================================
 * メモリ→メモリの 3 オペランド演算: void pie_xxx(const void* a, const void* b, void* out, uint32_t nvec)
 *   a0 = a, a1 = b, a2 = out, a3 = nvec    本体 4 命令 / ベクトル: ld, ld, op, st
 * ===================================================================== */
    .macro  OP3_BODY insn
    esp.vld.128.ip  q0, a0, 16
    esp.vld.128.ip  q1, a1, 16
    \insn   q2, q0, q1
0:  esp.vst.128.ip  q2, a2, 16
    .endm

    .macro  MEM_OP3 name, insn
    .global \name
    .type   \name, @function
    .balign 4
\name:
    li      t3, 0
    esp.movx.w.sar  t3
    CHUNKED_LOOP a3, t4, t5, t6, OP3_BODY \insn
    ret
    .endm

    MEM_OP3 pie_mul_s8,  esp.vmul.s8
    MEM_OP3 pie_mul_s16, esp.vmul.s16
    MEM_OP3 pie_add_s8,  esp.vadd.s8
    MEM_OP3 pie_add_s16, esp.vadd.s16
    MEM_OP3 pie_add_s32, esp.vadd.s32

/* =====================================================================
 * コピー: void pie_copy(void* dst, const void* src, uint32_t nvec)
 *   a0 = dst, a1 = src, a2 = nvec
 * ===================================================================== */
    .macro  COPY_BODY
    esp.vld.128.ip  q0, a1, 16
0:  esp.vst.128.ip  q0, a0, 16
    .endm

    .global pie_copy
    .type   pie_copy, @function
    .balign 4
pie_copy:
    CHUNKED_LOOP a2, t4, t5, t6, COPY_BODY
    ret

/* =====================================================================
 * uint32_t pie_bench_int32_mac(uint32_t iters)
 *   mul x4 → add x4 を 2 回 = 8 MAC / iter (4 本の独立アキュムレータ)
 * ===================================================================== */
    .macro  INT32_BODY
    mul     t3, a4, a5
    mul     t0, a4, a5
    mul     t1, a4, a5
    mul     t2, a4, a5
    add     a1, a1, t3
    add     a2, a2, t0
    add     a3, a3, t1
    add     t6, t6, t2
    mul     t3, a4, a5
    mul     t0, a4, a5
    mul     t1, a4, a5
    mul     t2, a4, a5
    add     a1, a1, t3
    add     a2, a2, t0
    add     a3, a3, t1
0:  add     t6, t6, t2
    .endm

    .global pie_bench_int32_mac
    .type   pie_bench_int32_mac, @function
    .balign 4
pie_bench_int32_mac:
    li      a4, 3
    li      a5, 5
    li      a1, 0
    li      a2, 0
    li      a3, 0
    li      t6, 0
    /* count = a0, chunks = t4, rem = t5, c256 = s8 は使えないので a0 を使い回す */
    srli    t4, a0, 8
    andi    t5, a0, CHUNK-1
    li      a0, CHUNK
    beqz    t4, 2f
1:
    esp.lp.setup 0, a0, 0f
    INT32_BODY
    addi    t4, t4, -1
    bnez    t4, 1b
2:
    beqz    t5, 3f
    esp.lp.setup 0, t5, 0f
    INT32_BODY
3:
    add     a0, a1, a2
    add     a0, a0, a3
    add     a0, a0, t6
    ret

/* =====================================================================
 * uint32_t pie_bench_fp32_madd(uint32_t iters)
 *   fmadd.s ft0..ft7 = fa0 * fa1 + ft  x2 = 16 FMA / iter (8 本の独立アキュムレータ)
 * ===================================================================== */
    .macro  FP32_BODY
    fmadd.s ft0, fa0, fa1, ft0
    fmadd.s ft1, fa0, fa1, ft1
    fmadd.s ft2, fa0, fa1, ft2
    fmadd.s ft3, fa0, fa1, ft3
    fmadd.s ft4, fa0, fa1, ft4
    fmadd.s ft5, fa0, fa1, ft5
    fmadd.s ft6, fa0, fa1, ft6
    fmadd.s ft7, fa0, fa1, ft7
    fmadd.s ft0, fa0, fa1, ft0
    fmadd.s ft1, fa0, fa1, ft1
    fmadd.s ft2, fa0, fa1, ft2
    fmadd.s ft3, fa0, fa1, ft3
    fmadd.s ft4, fa0, fa1, ft4
    fmadd.s ft5, fa0, fa1, ft5
    fmadd.s ft6, fa0, fa1, ft6
0:  fmadd.s ft7, fa0, fa1, ft7
    .endm

    .global pie_bench_fp32_madd
    .type   pie_bench_fp32_madd, @function
    .balign 4
pie_bench_fp32_madd:
    li      t3, 0x3F800000          /* 1.0f */
    fmv.w.x fa0, t3
    li      t3, 0x3F000000          /* 0.5f */
    fmv.w.x fa1, t3
    fmv.w.x ft0, zero
    fmv.w.x ft1, zero
    fmv.w.x ft2, zero
    fmv.w.x ft3, zero
    fmv.w.x ft4, zero
    fmv.w.x ft5, zero
    fmv.w.x ft6, zero
    fmv.w.x ft7, zero
    CHUNKED_LOOP a0, a1, a2, a3, FP32_BODY
    fadd.s  ft0, ft0, ft1
    fmv.x.w a0, ft0
    ret

#endif /* CONFIG_IDF_TARGET_ESP32P4 */
