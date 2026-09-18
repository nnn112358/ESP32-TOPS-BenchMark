/*
 * ESP32-S3 PIE (Processor Instruction Extensions) SIMD benchmark kernels
 *
 * Xtensa LX7 windowed ABI: a2..a7 = 引数, 戻り値 = a2, a8..a15 = 一時レジスタ
 * QR レジスタ q0..q7 (128bit) はコンパイラが使わないので自由に使える
 * ゼロオーバーヘッドループ (loopgtz) の本体は 256 バイト以内, 分岐なし
 *
 * PIE (ee.* 命令, q レジスタ) は ESP32-S3 のみ。無印 ESP32 (LX6) ではスカラー部分だけを組み立てる。
 */
#include "sdkconfig.h"
#if !CONFIG_IDF_TARGET_ESP32P4   /* Xtensa (ESP32 / ESP32-S3) のみ。P4 は pie_kernels_p4.S */

    .text

#if CONFIG_IDF_TARGET_ESP32S3

/* ---------- q0 = q1 = {0x01020304 x4} を作るマクロ ---------- */
    .macro  INIT_Q01
    movi    a8, 0x102
    slli    a8, a8, 16
    addi    a8, a8, 0x304
    ee.movi.32.q    q0, a8, 0
    ee.movi.32.q    q0, a8, 1
    ee.movi.32.q    q0, a8, 2
    ee.movi.32.q    q0, a8, 3
    mv.qr   q1, q0
    .endm

/* ---------- 同じ命令を 16 回並べるマクロ ---------- */
    .macro  REP16 insn:vararg
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
    \insn
    .endm

/* =====================================================================
 * レジスタ演算のみ: uint32_t pie_bench_s8_accx(uint32_t iters)
 * ===================================================================== */
    .align  4
    .global pie_bench_s8_accx
    .type   pie_bench_s8_accx, @function
pie_bench_s8_accx:
    entry   a1, 32
    INIT_Q01
    ee.zero.accx
    loopgtz a2, 0f
    REP16   ee.vmulas.s8.accx q0, q1
0:
    rur.accx_0  a2
    retw

/* =====================================================================
 * レジスタ演算のみ: uint32_t pie_bench_s16_accx(uint32_t iters)
 * ===================================================================== */
    .align  4
    .global pie_bench_s16_accx
    .type   pie_bench_s16_accx, @function
pie_bench_s16_accx:
    entry   a1, 32
    INIT_Q01
    ee.zero.accx
    loopgtz a2, 0f
    REP16   ee.vmulas.s16.accx q0, q1
0:
    rur.accx_0  a2
    retw

/* =====================================================================
 * 内積: uint32_t pie_dot_s8(uint32_t iters, const void* a, const void* b, uint32_t nvec)
 *   a2 = iters, a3 = a, a4 = b, a5 = nvec   (a, b とも (nvec+1)*16 バイト以上)
 *   ループ本体 2 命令 / 16 MAC: ロード+MAC 融合命令 + ロード
 * ===================================================================== */
    .macro  DOT_KERNEL name, mac_ld
    .align  4
    .global \name
    .type   \name, @function
\name:
    entry   a1, 32
    ee.zero.qacc
1:
    mov     a6, a3
    mov     a7, a4
    ee.vld.128.ip   q0, a6, 16
    ee.vld.128.ip   q1, a7, 16
    loopgtz a5, 0f
    \mac_ld q0, a6, 16, q0, q1
    ee.vld.128.ip   q1, a7, 16
0:
    addi    a2, a2, -1
    bnez    a2, 1b
    rur.qacc_l_0    a2
    retw
    .endm

    DOT_KERNEL pie_dot_s8,  ee.vmulas.s8.qacc.ld.ip
    DOT_KERNEL pie_dot_s16, ee.vmulas.s16.qacc.ld.ip

/* =====================================================================
 * メモリ→メモリの 3 オペランド演算: void pie_xxx(const void* a, const void* b, void* out, uint32_t nvec)
 *   a2 = a, a3 = b, a4 = out, a5 = nvec
 *   ループ本体 4 命令 / ベクトル: ld, ld, op, st
 * ===================================================================== */
    .macro  MEM_OP3 name, insn
    .align  4
    .global \name
    .type   \name, @function
\name:
    entry   a1, 32
    movi    a8, 0
    wsr.sar a8
    isync
    loopgtz a5, 0f
    ee.vld.128.ip   q0, a2, 16
    ee.vld.128.ip   q1, a3, 16
    \insn   q2, q0, q1
    ee.vst.128.ip   q2, a4, 16
0:
    retw
    .endm

    MEM_OP3 pie_mul_s8,  ee.vmul.s8
    MEM_OP3 pie_mul_s16, ee.vmul.s16
    MEM_OP3 pie_add_s8,  ee.vadds.s8
    MEM_OP3 pie_add_s16, ee.vadds.s16
    MEM_OP3 pie_add_s32, ee.vadds.s32

/* =====================================================================
 * コピー: void pie_copy(void* dst, const void* src, uint32_t nvec)
 *   a2 = dst, a3 = src, a4 = nvec
 * ===================================================================== */
    .align  4
    .global pie_copy
    .type   pie_copy, @function
pie_copy:
    entry   a1, 32
    loopgtz a4, 0f
    ee.vld.128.ip   q0, a3, 16
    ee.vst.128.ip   q0, a2, 16
0:
    retw

#endif /* CONFIG_IDF_TARGET_ESP32S3 */

    .literal_position
/* =====================================================================
 * uint32_t pie_bench_int32_mac(uint32_t iters)
 *   mull x4 → add x4 を 2 回 = 8 MAC / iter (4 本の独立アキュムレータ)
 * ===================================================================== */
    .align  4
    .global pie_bench_int32_mac
    .type   pie_bench_int32_mac, @function
pie_bench_int32_mac:
    entry   a1, 32
    movi    a10, 3
    movi    a11, 5
    movi    a12, 0
    movi    a13, 0
    movi    a14, 0
    movi    a15, 0
    loopgtz a2, 0f
    mull    a8, a10, a11
    mull    a9, a10, a11
    mull    a6, a10, a11
    mull    a7, a10, a11
    add     a12, a12, a8
    add     a13, a13, a9
    add     a14, a14, a6
    add     a15, a15, a7
    mull    a8, a10, a11
    mull    a9, a10, a11
    mull    a6, a10, a11
    mull    a7, a10, a11
    add     a12, a12, a8
    add     a13, a13, a9
    add     a14, a14, a6
    add     a15, a15, a7
0:
    add     a2, a12, a13
    add     a2, a2, a14
    add     a2, a2, a15
    retw

/* =====================================================================
 * uint32_t pie_bench_fp32_madd(uint32_t iters)
 *   madd.s f0..f7 += f8 * f9  x2 = 16 FMA / iter (8 本の独立アキュムレータ)
 * ===================================================================== */
    .align  4
    .global pie_bench_fp32_madd
    .type   pie_bench_fp32_madd, @function
pie_bench_fp32_madd:
    entry   a1, 32
    movi    a8, 0x3F8           /* 1.0f = 0x3F800000 */
    slli    a8, a8, 20
    wfr     f8, a8
    movi    a8, 0x3F0           /* 0.5f = 0x3F000000 */
    slli    a8, a8, 20
    wfr     f9, a8
    movi    a8, 0
    wfr     f0, a8
    wfr     f1, a8
    wfr     f2, a8
    wfr     f3, a8
    wfr     f4, a8
    wfr     f5, a8
    wfr     f6, a8
    wfr     f7, a8
    loopgtz a2, 0f
    madd.s  f0, f8, f9
    madd.s  f1, f8, f9
    madd.s  f2, f8, f9
    madd.s  f3, f8, f9
    madd.s  f4, f8, f9
    madd.s  f5, f8, f9
    madd.s  f6, f8, f9
    madd.s  f7, f8, f9
    madd.s  f0, f8, f9
    madd.s  f1, f8, f9
    madd.s  f2, f8, f9
    madd.s  f3, f8, f9
    madd.s  f4, f8, f9
    madd.s  f5, f8, f9
    madd.s  f6, f8, f9
    madd.s  f7, f8, f9
0:
    add.s   f0, f0, f1
    rfr     a2, f0
    retw

#endif /* !CONFIG_IDF_TARGET_ESP32P4 */
