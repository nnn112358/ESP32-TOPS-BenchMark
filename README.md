# ESP32-TOPS-BenchMark

**English** | [日本語](README.ja.md)

<img width="371" height="354" alt="M5Stack CoreS3 (ESP32-S3) running the benchmark" src="https://github.com/user-attachments/assets/bc77c897-39e8-48af-a035-f3671b9e509e" />
<img width="710" height="525" alt="M5Stack Tab5 (ESP32-P4) running the benchmark" src="https://github.com/user-attachments/assets/cacc7a59-4085-4a09-9829-391468d0a618" />

A [vkpeak](https://github.com/nihui/vkpeak)-style compute (GOPS / TOPS) and memory-bandwidth benchmark for
M5Stack boards based on ESP32, ESP32-S3 and ESP32-P4.
It runs the same workloads with the PIE (Processor Instruction Extensions) SIMD instructions available on
ESP32-S3 / ESP32-P4 and with plain C code (no PIE), shows both side by side on the display via M5Unified,
and streams the results (including a machine-readable CSV block) over serial.

## Supported boards

| PlatformIO env | Board | CPU | PIE SIMD |
|---|---|---|---|
| `m5stack-cores3` | M5Stack CoreS3 | ESP32-S3 (Xtensa LX7, 240 MHz x2, 8 MB PSRAM) | yes (`ee.*`) |
| `m5stack-tab5` | M5Stack Tab5 | ESP32-P4 (RISC-V, 360 MHz x2, 32 MB PSRAM) | yes (`esp.*`) |
| `m5stack-core2` | M5Stack Core / Core2 / Fire | ESP32 (Xtensa LX6, 240 MHz x2) | no |

- M5Unified detects the board at runtime. Rows that need PSRAM are skipped on boards without it.
- The layout is designed for 320x240 and scaled by an integer factor on larger displays (Tab5: 1280x720, x3).

## Results

Single-core values. A MAC counts as 2 ops (multiply + add), the same convention as vkpeak and GPU vendors' TOPS figures.

| Test | Tab5 (ESP32-P4) | CoreS3 (ESP32-S3) | Core2 (ESP32) |
|---|---|---|---|
| int8-mac-reg (PIE compute peak) | 11.46 GOPS | 7.65 GOPS | - |
| int8-mac-sram  PIE / C | 3.75 / 0.143 GOPS (26x) | 2.54 / 0.068 GOPS (37x) | - / 0.049 GOPS |
| int16-mac-sram PIE / C | 1.88 / 0.151 GOPS (12x) | 1.27 / 0.083 GOPS (15x) | - / 0.066 GOPS |
| int8-add-sram  PIE / C | 1.13 / 0.023 GOPS (50x) | 0.76 / 0.015 GOPS (51x) | - / 0.010 GOPS |
| int8-mac-psram PIE / C | 0.160 / 0.076 GOPS (2.1x) | 0.028 / 0.025 GOPS (1.1x) | - / 0.018 GOPS |
| int32-scalar (mul+add) | 0.358 GOPS | 0.239 GOPS | 0.239 GOPS |
| fp32-scalar (FMA) | 0.716 GFLOPS | 0.478 GFLOPS | 0.478 GFLOPS |
| copy-sram  PIE / memcpy | 2792 / 559 MB/s | 1267 / 381 MB/s | - / 191 MB/s |
| copy-psram | 61 MB/s | 9.5 MB/s | 9.1 MB/s |
| **DEVICE PEAK (2 cores)** | **22.91 GOPS = 0.0229 TOPS** | **15.28 GOPS = 0.0153 TOPS** | **0.48 GOPS = 0.0005 TOPS** |

- The PIE int8 MAC instruction issues once per cycle (16 lanes), so the measured peak is within 1% of
  clock x 2 cores x 32 ops. The only thing that raises the ceiling is the clock: Tab5 vs CoreS3 is exactly
  the clock ratio (360 / 240 = 1.5x).
- On data in internal SRAM, PIE is 12-50x faster than plain C. On data in PSRAM the bandwidth
  (10-60 MB/s) dominates and PIE barely helps.
- Full per-board output is in [`results/`](results/), together with vkpeak results from a PC measured with
  the same convention for reference (RTX 3070 Laptop: int8-matrix about 155 TOPS).

## Tests

| Test | What it does | PIE side | no-PIE side |
|---|---|---|---|
| int8/int16-mac-reg | register-only MAC (compute peak) | `vmulas.s8/s16` (S3: ACCX, P4: XACC) | - |
| int8/int16-mac-sram | dot product of 8 KB + 8 KB in SRAM | `vmulas.*.qacc.ld.ip` + `vld.128` | C `acc += a[i]*b[i]` |
| int8-mac-psram | dot product of 256 KB + 256 KB in PSRAM | same | same |
| int8/int16-mul-sram | element-wise multiply out = a*b | `vld`, `vmul`, `vst` | C |
| int8/16/32-add-sram | element-wise saturating add | `vld`, `vadd(s)`, `vst` | C (clamp) |
| int32-scalar | scalar integer MAC | - | `mull`/`mul` + `add` (asm) |
| fp32-scalar | scalar FPU FMA | - | `madd.s` / `fmadd.s` (asm) |
| copy-* | copy bandwidth (SRAM 16 KB, PSRAM 512 KB) | `vld.128` / `vst.128` | `memcpy` |

- Each test runs for about 100 ms, twice, and the best value is kept.
- The display shows single-core PIE / no-PIE / ratio. Serial output also includes the sum of both cores
  running the same kernel simultaneously.
- The no-PIE side is ordinary C compiled with `-O2` (gcc does not auto-vectorize to PIE).

## Build and flash

Uses [PlatformIO](https://platformio.org/) with [pioarduino](https://github.com/pioarduino/platform-espressif32)
(arduino-esp32 3.x). The official `espressif32` platform is stuck on arduino-esp32 2.x and cannot target the
ESP32-P4, so `platformio.ini` points at pioarduino.

```
pio run -e m5stack-cores3 -t upload   # CoreS3 (ESP32-S3)        /dev/ttyACM0
pio run -e m5stack-tab5   -t upload   # Tab5 (ESP32-P4)          /dev/ttyACM0
pio run -e m5stack-core2  -t upload   # Core/Core2/Fire (ESP32)  /dev/ttyUSB0
pio device monitor -e m5stack-cores3  # watch the results (115200 bps)
```

The benchmark runs automatically at boot. Tap the screen (or press a button on Core/Core2), or send `r`
over serial, to run it again.

## Fetching results from a PC

After each run the firmware prints the table followed by a machine-readable CSV block
(`=== CSV ===` ... `=== END ===`). The bundled script sends `r`, waits for the block, rebuilds the table
from the CSV and saves it.

```
uv run --with pyserial tools/get_result.py                          # CoreS3 / Tab5 (/dev/ttyACM0)
uv run --with pyserial tools/get_result.py -p /dev/ttyUSB0          # classic ESP32
uv run --with pyserial tools/get_result.py -o result.txt --csv result.csv
uv run --with pyserial tools/get_result.py --reset                  # reset the board and capture the boot run
```

CSV columns: `chip,mhz,test,pie_1core,nopie_1core,pie_2core,nopie_2core,unit` (empty when not applicable).

## Layout

```
platformio.ini            3 environments (m5stack-cores3 / m5stack-tab5 / m5stack-core2)
src/main.cpp              setup / loop and the run sequence (just calls the modules below)
src/kernels.h             kernel prototypes and ops-per-iteration constants
src/pie_kernels.S         ESP32-S3 / ESP32 assembly kernels (Xtensa windowed ABI, loopgtz zero-overhead loop)
src/pie_kernels_p4.S      ESP32-P4 assembly kernels (RISC-V, esp.lp.setup hardware loop)
src/c_kernels.cpp         plain C scalar kernels for the no-PIE side
src/buffers.{h,cpp}       allocation / initialization of the SRAM and PSRAM test buffers
src/benchmarks.{h,cpp}    the test table (PIE / no-PIE kernel pairs, ops, units) and peak calculation
src/measure.{h,cpp}       calibration and single-core / dual-core measurement (worker task)
src/display.{h,cpp}       M5Unified display (320x240 base, integer scaling for larger screens)
src/report.{h,cpp}        serial output (table + CSV block)
tools/get_result.py       PC-side result fetcher (pyserial)
results/                  measured output per board (txt / csv) and PC vkpeak results
```

## Implementation notes

- **S3 (Xtensa)**: PIE instructions are `ee.*`; the QR registers q0-q7 are never used by the compiler,
  so they are free. The `loopgtz` label is the first instruction *after* the loop body.
- **P4 (RISC-V)**: PIE instructions are `esp.*`; only a0-a5, s0-s1, s8-s11 and t3-t6 may be used as
  address/count registers, and `.option norvc` is required. The `esp.lp.setup` label is the *last*
  instruction of the body and a zero count must be skipped by hand.
  On real hardware (rev1.0) large counts (thousands or more) did not loop correctly, so the kernels use an
  outer software loop around an inner hardware loop of 256.
- References: esp-dl [ESP32-S3 PIE SIMD skill](https://github.com/espressif/esp-dl/blob/master/tools/agents/skills/esp32s3-pie-simd/SKILL.md) /
  [ESP32-P4 PIE SIMD skill](https://github.com/espressif/esp-dl/blob/master/tools/agents/skills/esp32p4-pie-simd/SKILL.md)

## Author

- nnn112358 (<https://github.com/nnn112358>)
- Developed with Claude Code (Anthropic)

## License

MIT — see [LICENSE](LICENSE)
