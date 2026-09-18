# M5Stack-BenchMark

M5Stack 各機種 (ESP32 / ESP32-S3 / ESP32-P4) の演算性能 (GOPS / TOPS) とメモリ帯域を測定するベンチマークです。
ESP32-S3 / ESP32-P4 が持つ PIE (Processor Instruction Extensions) SIMD 命令を使った場合と、
通常の C コード (PIE なし) で同じ処理をした場合を並べて表示し、
[vkpeak](https://github.com/nihui/vkpeak) 風の表を M5Unified の画面とシリアルに出力します。

*A vkpeak-style compute / memory-bandwidth benchmark for M5Stack boards (ESP32, ESP32-S3, ESP32-P4).
Measures PIE SIMD vs plain C throughput, shows results on the display via M5Unified, and streams a CSV block over serial.*

## 対応機種

| PlatformIO env | 機種 | CPU | PIE SIMD |
|---|---|---|---|
| `m5stack-cores3` | M5Stack CoreS3 | ESP32-S3 (Xtensa LX7, 240 MHz x2, PSRAM 8 MB) | あり (`ee.*`) |
| `m5stack-tab5` | M5Stack Tab5 | ESP32-P4 (RISC-V, 360 MHz x2, PSRAM 32 MB) | あり (`esp.*`) |
| `m5stack-core2` | M5Stack Core / Core2 / Fire | ESP32 (Xtensa LX6, 240 MHz x2) | なし |

- M5Unified が機種を自動判別します。PSRAM の無い機種では PSRAM を使う行をスキップします。
- 画面は 320x240 を基準に、大きい画面 (Tab5 1280x720) では整数倍に拡大します。

## 実測結果

1 コアの値。MAC は 2 ops (乗算 + 加算) として数えています (vkpeak や GPU ベンダの TOPS 公称値と同じ流儀)。

| 項目 | Tab5 (ESP32-P4) | CoreS3 (ESP32-S3) | Core2 (ESP32) |
|---|---|---|---|
| int8-mac-reg (PIE 演算ピーク) | 11.46 GOPS | 7.65 GOPS | - |
| int8-mac-sram  PIE / C | 3.75 / 0.143 GOPS (26x) | 2.54 / 0.068 GOPS (37x) | - / 0.049 GOPS |
| int16-mac-sram PIE / C | 1.88 / 0.151 GOPS (12x) | 1.27 / 0.083 GOPS (15x) | - / 0.066 GOPS |
| int8-add-sram  PIE / C | 1.13 / 0.023 GOPS (50x) | 0.76 / 0.015 GOPS (51x) | - / 0.010 GOPS |
| int8-mac-psram PIE / C | 0.160 / 0.076 GOPS (2.1x) | 0.028 / 0.025 GOPS (1.1x) | - / 0.018 GOPS |
| int32-scalar (mul+add) | 0.358 GOPS | 0.239 GOPS | 0.239 GOPS |
| fp32-scalar (FMA) | 0.716 GFLOPS | 0.478 GFLOPS | 0.478 GFLOPS |
| copy-sram  PIE / memcpy | 2792 / 559 MB/s | 1267 / 381 MB/s | - / 191 MB/s |
| copy-psram | 61 MB/s | 9.5 MB/s | 9.1 MB/s |
| **DEVICE PEAK (2 コア合計)** | **22.91 GOPS = 0.0229 TOPS** | **15.28 GOPS = 0.0153 TOPS** | **0.48 GOPS = 0.0005 TOPS** |

- PIE の int8 MAC 命令は 1 サイクルに 1 命令 (16 レーン) 発行でき、実測はクロック x 2 コア x 32 ops の理論値の 99% 以上です。
  上限を決めるのはクロックだけで、Tab5 と CoreS3 の差はクロック比 (360 / 240 = 1.5 倍) そのものです。
- SRAM 上のデータでは PIE が C の 12〜50 倍速くなります。PSRAM 上のデータは帯域 (10〜60 MB/s) が律速で PIE の効果はほぼ出ません。
- 各機種の全結果は [`results/`](results/) にあります。参考として同じ流儀で測った PC 側の vkpeak 結果 (RTX 3070 Laptop: int8-matrix 約 155 TOPS) も置いています。

## 測定項目

| 項目 | 内容 | PIE 側 | noPIE 側 |
|---|---|---|---|
| int8/int16-mac-reg | レジスタのみの MAC (演算ピーク) | `vmulas.s8/s16` (S3: ACCX, P4: XACC) | - |
| int8/int16-mac-sram | SRAM 上 8 KB + 8 KB の内積 | `vmulas.*.qacc.ld.ip` + `vld.128` | C `acc += a[i]*b[i]` |
| int8-mac-psram | PSRAM 上 256 KB + 256 KB の内積 | 同上 | 同上 |
| int8/int16-mul-sram | 要素毎の乗算 out = a*b | `vld`, `vmul`, `vst` | C |
| int8/16/32-add-sram | 要素毎の飽和加算 | `vld`, `vadd(s)`, `vst` | C (clamp) |
| int32-scalar | スカラー整数 MAC | - | `mull`/`mul` + `add` (asm) |
| fp32-scalar | スカラー FPU FMA | - | `madd.s` / `fmadd.s` (asm) |
| copy-* | コピー帯域 (SRAM 16 KB, PSRAM 512 KB) | `vld.128` / `vst.128` | `memcpy` |

- 各項目は約 100 ms x 2 回計測して最良値を採用します。
- 画面には 1 コアの PIE / noPIE / 倍率を、シリアルには 2 コア同時実行 (両コアで同じカーネルを走らせた合計) も出力します。
- noPIE 側は `-O2` でコンパイルした普通の C ループです (gcc は PIE への自動ベクトル化を行いません)。

## ビルド・書き込み

[PlatformIO](https://platformio.org/) と [pioarduino](https://github.com/pioarduino/platform-espressif32) (arduino-esp32 3.x) を使います。
公式の `espressif32` プラットフォームは arduino-esp32 2.x で止まっており ESP32-P4 を扱えないため、`platformio.ini` で pioarduino を指定しています。

```
pio run -e m5stack-cores3 -t upload   # CoreS3 (ESP32-S3)       /dev/ttyACM0
pio run -e m5stack-tab5   -t upload   # Tab5 (ESP32-P4)         /dev/ttyACM0
pio run -e m5stack-core2  -t upload   # Core/Core2/Fire (ESP32) /dev/ttyUSB0
pio device monitor -e m5stack-cores3  # シリアルで結果を見る (115200 bps)
```

起動すると自動で測定します。画面タップ (Core/Core2 はボタン) またはシリアルに `r` を送ると再測定します。

## PC から結果を取得する

測定後、表に続けて機械可読な CSV ブロック (`=== CSV ===` 〜 `=== END ===`) を出力します。
付属スクリプトが `r` を送って測定させ、CSV から表を再構成して保存します。

```
uv run --with pyserial tools/get_result.py                          # CoreS3 / Tab5 (/dev/ttyACM0)
uv run --with pyserial tools/get_result.py -p /dev/ttyUSB0          # 無印 ESP32
uv run --with pyserial tools/get_result.py -o result.txt --csv result.csv
uv run --with pyserial tools/get_result.py --reset                  # リセットして起動時の測定を取得
```

CSV の列: `chip,mhz,test,pie_1core,nopie_1core,pie_2core,nopie_2core,unit` (未対応の項目は空欄)。

## ファイル構成

```
platformio.ini            3 環境 (m5stack-cores3 / m5stack-tab5 / m5stack-core2)
src/main.cpp              setup / loop と測定の進行 (各モジュールを呼ぶだけ)
src/kernels.h             計測カーネルのプロトタイプと 1 イテレーションあたりの演算数
src/pie_kernels.S         ESP32-S3 / ESP32 用アセンブリカーネル (Xtensa windowed ABI, loopgtz ゼロオーバーヘッドループ)
src/pie_kernels_p4.S      ESP32-P4 用アセンブリカーネル (RISC-V, esp.lp.setup ハードウェアループ)
src/c_kernels.cpp         noPIE 比較用の C スカラーカーネル
src/buffers.{h,cpp}       計測用バッファ (内部 SRAM / PSRAM) の確保と初期化
src/benchmarks.{h,cpp}    測定項目の表 (PIE 版 / noPIE 版カーネルのペア、演算数、単位) とピーク算出
src/measure.{h,cpp}       キャリブレーションと 1 コア / 2 コア同時計測 (ワーカータスク)
src/display.{h,cpp}       M5Unified 画面表示 (320x240 基準、大画面は整数倍)
src/report.{h,cpp}        シリアル出力 (表 + CSV ブロック)
tools/get_result.py       PC 側の結果取得スクリプト (pyserial)
results/                  各機種の実測結果 (txt / csv) と PC 側 vkpeak の結果
```

## 実装メモ

- **S3 (Xtensa)**: PIE 命令は `ee.*`、QR レジスタ q0〜q7 はコンパイラが使わないので自由に使えます。`loopgtz` のラベルは本体の「次」の命令です。
- **P4 (RISC-V)**: PIE 命令は `esp.*`、使える AR は a0〜a5, s0〜s1, s8〜s11, t3〜t6 のみで、`.option norvc` が必要です。
  `esp.lp.setup` のラベルは本体の「最後」の命令で、回数 0 は自分で除外します。
  実機 (rev1.0) では回数が大きい (数千以上) と正しく回らなかったため、外側ソフトウェアループ x 内側 256 回の構造にしています。
- 参考資料: esp-dl の [ESP32-S3 PIE SIMD skill](https://github.com/espressif/esp-dl/blob/master/tools/agents/skills/esp32s3-pie-simd/SKILL.md) /
  [ESP32-P4 PIE SIMD skill](https://github.com/espressif/esp-dl/blob/master/tools/agents/skills/esp32p4-pie-simd/SKILL.md)

## Author

- nnn112358 (<https://github.com/nnn112358>)
- Developed with Claude Code (Anthropic)

## License

MIT — see [LICENSE](LICENSE)
