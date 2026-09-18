#!/usr/bin/env python3
"""ESP32 / ESP32-S3 TOPS ベンチマークの結果をシリアル経由で PC に取得する。

使い方:
  uv run --with pyserial tools/get_result.py                  # /dev/ttyACM0 に "r" を送って測定、結果を表示
  uv run --with pyserial tools/get_result.py -p /dev/ttyUSB0  # ポート指定 (無印 ESP32 など)
  uv run --with pyserial tools/get_result.py --reset          # "r" の代わりにボードをリセットして起動時の測定を取得
  uv run --with pyserial tools/get_result.py -o result.txt --csv result.csv

ファームウェアは測定後に "=== CSV ===" ～ "=== END ===" の機械可読ブロックを出力する。
このスクリプトは CSV ブロックから表を再構成する (USB CDC 接続直後の先頭欠落に影響されない)。
"""
import argparse
import csv
import io
import sys
import time

import serial


def capture(port: str, baud: int, reset: bool, timeout: float) -> str:
    s = serial.Serial(port, baud, timeout=0.5)
    if reset:
        s.setDTR(False)
        s.setRTS(True)
        time.sleep(0.1)
        s.setRTS(False)
    else:
        time.sleep(0.5)
        s.reset_input_buffer()
        s.write(b"r\n")
        s.flush()
    t0 = time.time()
    buf = b""
    while time.time() - t0 < timeout:
        d = s.read(4096)
        if d:
            buf += d
            if b"=== END ===" in buf:
                break
    s.close()
    return buf.decode("utf-8", "replace")


def fmt(v: str, unit: str) -> str:
    if v == "":
        return "-"
    return f"{float(v):.1f}" if unit == "MB/s" else f"{float(v):.3f}"


def build_table(rows: list[dict]) -> str:
    chip, mhz = rows[0]["chip"], rows[0]["mhz"]
    out = [f"device       = {chip}, {mhz} MHz, 2 cores",
           f"{'':16s} {'PIE-1c':>9s} {'noPIE-1c':>9s} {'PIE-2c':>9s} {'noPIE-2c':>9s} {'ratio':>7s}  unit"]
    peak_pie = peak_nopie = 0.0
    test_pie = test_nopie = "-"
    for r in rows:
        u = r["unit"]
        p1, n1, p2, n2 = (r["pie_1core"], r["nopie_1core"], r["pie_2core"], r["nopie_2core"])
        ratio = f"{float(p1) / float(n1):6.1f}x" if p1 and n1 and float(n1) > 0 else f"{'-':>7s}"
        out.append(f"{r['test']:16s} {fmt(p1, u):>9s} {fmt(n1, u):>9s} {fmt(p2, u):>9s} {fmt(n2, u):>9s} {ratio}  {u}")
        if u == "GOPS":
            if p2 and float(p2) > peak_pie:
                peak_pie, test_pie = float(p2), r["test"]
            if n2 and float(n2) > peak_nopie:
                peak_nopie, test_nopie = float(n2), r["test"]
    out.append(f"peak PIE     = {peak_pie:.2f} GOPS = {peak_pie / 1000:.4f} TOPS (2 cores, {test_pie})" if peak_pie > 0
               else f"peak PIE     = n/a ({chip} has no PIE)")
    out.append(f"peak noPIE   = {peak_nopie:.2f} GOPS = {peak_nopie / 1000:.4f} TOPS (2 cores, {test_nopie})")
    dev, kind, test = ((peak_pie, "PIE", test_pie) if peak_pie >= peak_nopie else (peak_nopie, "noPIE", test_nopie))
    out.append(f"DEVICE PEAK  = {dev:.2f} GOPS = {dev / 1000:.4f} TOPS ({kind} {test}, 2 cores)")
    return "\n".join(out)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-p", "--port", default="/dev/ttyACM0")
    ap.add_argument("-b", "--baud", type=int, default=115200)
    ap.add_argument("--reset", action="store_true", help="'r' 送信ではなくボードをリセットして測定させる")
    ap.add_argument("-t", "--timeout", type=float, default=180.0, help="待ち時間の上限 [s]")
    ap.add_argument("-o", "--out", help="表形式の結果を保存するファイル")
    ap.add_argument("--csv", help="CSV を保存するファイル")
    a = ap.parse_args()

    txt = capture(a.port, a.baud, a.reset, a.timeout)
    if "=== CSV ===" not in txt or "=== END ===" not in txt:
        print("error: 結果ブロック (=== CSV === ～ === END ===) を受信できませんでした", file=sys.stderr)
        print(txt)
        return 1

    csv_text = txt.split("=== CSV ===", 1)[1].split("=== END ===", 1)[0].strip()
    rows = list(csv.DictReader(io.StringIO(csv_text)))
    if not rows:
        print("error: CSV が空です", file=sys.stderr)
        return 1

    table = build_table(rows)
    print(table)
    if a.out:
        with open(a.out, "w") as f:
            f.write(table + "\n")
        print(f"saved: {a.out}")
    if a.csv:
        with open(a.csv, "w") as f:
            f.write(csv_text + "\n")
        print(f"saved: {a.csv}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
