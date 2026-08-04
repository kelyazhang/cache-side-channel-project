# Week 1 Single-Thread Latency Calibration

## Basic Information

- Date: 2026-08-04
- Experiment ID: week1-single-thread-latency-calibration
- Source code:
  - `src/week1/flush_reload_rdtscp.c`
  - `src/week1/flush_reload_rdtsc.c`
- Raw data:
  - `data/raw/week1/calibration_rdtscp_50000.csv`
  - `data/raw/week1/calibration_rdtsc_50000.csv`
- Git commit: record after this change is committed
- CPU binding: logical CPU 5 in both source files

## Objective

Organize and preserve the first single-thread Flush+Reload calibration
artifacts required by the Week 1 project milestone. The intended result is a
trustworthy hit/miss timing primitive before implementing the Week 2 covert
channel.

## Controlled Implementations

The two source files use the same mapped shared read-only page, target cache
line, CPU affinity, warm-up structure, HIT/MISS construction, and NOP delays.
The timing instruction is the controlled variable:

- `flush_reload_rdtscp.c`: `RDTSCP` with fence ordering.
- `flush_reload_rdtsc.c`: `RDTSC` with the corresponding fence sequence.

The source files are archived under `src/week1/` and were not modified during
the repository reorganization. The original CSV files were copied unchanged
into `data/raw/week1/`.

## Observed Raw Data

Statistics below are calculated directly from the archived CSV files. Values
are invariant TSC ticks.

| Dataset | Rows | HIT min | HIT median | HIT mean | HIT max | MISS min | MISS median | MISS mean | MISS max |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| `calibration_rdtscp_50000.csv` | 50,000 | 90 | 100 | 99.33 | 156 | 824 | 1,094 | 1,117.76 | 2,630 |
| `calibration_rdtsc_50000.csv` | 100 | 76 | 78 | 78.06 | 84 | 936 | 996 | 1,021.94 | 1,544 |

The `rdtscp` dataset contains 50,000 data rows. The `rdtsc` filename contains
`50000`, but the file contains 100 data rows; the observed row count is kept
as the authoritative value.

## Interpretation

Both datasets show a substantial separation between the central HIT and MISS
latencies. The separation is promising for threshold calibration, but the
datasets should not yet be treated as a final threshold study because the
sample counts differ and no histogram or repeated-environment analysis has
been committed yet.

## Reproduction Commands

```bash
mkdir -p build
gcc -O3 -std=gnu11 -Wall -Wextra -march=native \
    -fno-pie -no-pie src/week1/flush_reload_rdtscp.c \
    -o build/flush_reload_rdtscp

gcc -O3 -std=gnu11 -Wall -Wextra -march=native \
    -fno-pie -no-pie src/week1/flush_reload_rdtsc.c \
    -o build/flush_reload_rdtsc
```

Run each executable with `taskset` on the same P-core selected in the source.
Store every new run under a new filename in `data/raw/week1/` and document
the exact command and environment here or in a follow-up dated note.

## Next Step

Generate hit/miss histograms, select and justify a cache threshold, repeat the
comparison with matched sample counts, and record the controls for the Week 1
check-in.
