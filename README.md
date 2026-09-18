# esp32s3_tops — M5Stack (ESP32 / ESP32-S3) TOPS ベンチマーク (PIE あり / なし)

ESP32-S3 の PIE (Processor Instruction Extensions) SIMD 命令を使った場合と、
通常の C スカラーコード (PIE なし) で同じ処理を行った場合のスループット
(GOPS / TOPS) と倍率を測定し、vkpeak 風の表を M5Unified の画面と
シリアル (115200bps) に表示します。

無印 ESP32 (M5Stack Core / Core2 / Fire 等) でも動作します。ESP32 には PIE が
無いので PIE 列は「-」になり、noPIE (C スカラー) の結果だけが表示されます。
PSRAM の無い機種では PSRAM を使う行をスキップします。

## ビルド・書き込み

```
cd esp32s3_tops
pio run -e m5stack-cores3 -t upload   # CoreS3 (ESP32-S3)    /dev/ttyACM0
pio run -e m5stack-core2  -t upload   # Core/Core2/Fire (ESP32) /dev/ttyUSB0
pio device monitor -e m5stack-cores3  # シリアルで結果を見る
```

画面をタップ (Core/Core2 はボタン) すると再測定します。画面には 1 コアの PIE / noPIE / 倍率、
シリアルには 2 コア同時実行の合計も出力します。

## PC から結果を取得する

シリアルに `r` を送ると再測定し、表に続けて機械可読な CSV ブロック
(`=== CSV ===` ～ `=== END ===`) を出力します。付属スクリプトで取得できます。

```
uv run --with pyserial tools/get_result.py                       # CoreS3 (/dev/ttyACM0)
uv run --with pyserial tools/get_result.py -p /dev/ttyUSB0       # 無印 ESP32
uv run --with pyserial tools/get_result.py -o result.txt --csv result.csv
uv run --with pyserial tools/get_result.py --reset               # リセットして起動時の測定を取得
```

CSV の列: `chip,mhz,test,pie_1core,nopie_1core,pie_2core,nopie_2core,unit` (未対応の項目は空欄)。

## 測定項目

| 項目 | 内容 | PIE 側の命令 | noPIE 側 |
|---|---|---|---|
| int8/int16-mac-reg | レジスタのみの MAC (PIE の演算ピーク) | ee.vmulas.s8/s16.accx | - |
| int8/int16-mac-sram | SRAM 上 8KB+8KB の内積 | ee.vmulas.*.qacc.ld.ip + ee.vld | C `acc += a[i]*b[i]` |
| int8-mac-psram | PSRAM 上 256KB+256KB の内積 | 同上 | 同上 |
| int8/int16-mul-sram | 要素毎の乗算 out=a*b | ee.vld, ee.vmul, ee.vst | C |
| int8/16/32-add-sram | 要素毎の飽和加算 | ee.vld, ee.vadds, ee.vst | C (clamp) |
| int32-scalar | スカラー整数 MAC | - | mull + add (asm) |
| fp32-scalar | スカラー FPU FMA | - | madd.s (asm) |
| copy-* | コピー帯域 (SRAM 16KB, PSRAM 512KB) | ee.vld.128 / ee.vst.128 | memcpy |

- **MAC は 2 ops** として数えます (vkpeak や GPU ベンダの TOPS 公称値と同じ流儀)。
- noPIE 側は `-O2` でコンパイルした普通の C ループです (xtensa gcc は PIE への自動ベクトル化を行いません)。
- 各項目は約 100ms × 2 回計測して最良値を採用します。

## ファイル

- `src/pie_kernels.S` — PIE SIMD / スカラーのアセンブリカーネル (windowed ABI, `loopgtz` ゼロオーバーヘッドループ)
- `src/c_kernels.cpp` — noPIE 比較用の C スカラーカーネル
- `src/bench.h` — カーネルのプロトタイプと演算数の定義
- `src/main.cpp` — 計測ハーネス (キャリブレーション、デュアルコア実行)、M5Unified 表示、シリアル出力
- `tools/get_result.py` — PC 側の結果取得スクリプト (pyserial)

参考: [esp-dl ESP32-S3 PIE SIMD skill](https://github.com/espressif/esp-dl/blob/master/tools/agents/skills/esp32s3-pie-simd/SKILL.md)
