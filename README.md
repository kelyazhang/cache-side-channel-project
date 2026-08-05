# Cache Side-Channel Project

This repository documents a staged cache side-channel project carried out on
a local Intel/Linux system. The work follows the project brief:
first establish a trustworthy timing primitive, then use it to build and
measure a covert channel, recover a classical cryptographic key, and finally
characterize a post-quantum cryptography (PQC) leakage. Weeks 5 and 6 are
extensions for Prime+Probe and a larger research target.

## Project Roadmap

| Week | Objective | Planned evidence |
|---|---|---|
| 1 | Build and validate a Flush+Reload measurement primitive. | Hit/miss latency data, a bimodal histogram, a threshold, and controls. |
| 2 | Build a two-process cache covert channel. | Bit error rate, capacity in bits/sec, and CPU-topology comparison. |
| 3 | Recover an AES key with a software T-table victim. | Validated key recovery, trace count, success rate, and assumptions. |
| 4 | Add controls, write the report, present the work, and characterize a PQC leak. | Reproducible AES results and a short ML-KEM/Kyber leakage study. |
| 5 | Learn Prime+Probe and construct eviction sets without shared memory. | Eviction-set builder, covert channel, and comparison with Flush+Reload. |
| 6 | Start a larger research target such as a local-LLM cache side channel. | Partial prompt reconstruction or an equivalent measured result. |

The core milestone is Week 4. Weeks 5 and 6 are intentionally elastic and do
not replace the Week 4 report and presentation.

## Week 1 Status

As of **2026-08-04**, the Week 1 single-thread calibration code has been
organized into a dedicated weekly source directory. Two controlled timing
variants are preserved:

- `flush_reload_rdtscp.c` uses `RDTSCP` with `LFENCE` ordering.
- `flush_reload_rdtsc.c` uses `RDTSC` while keeping the mapping, CPU affinity,
  HIT/MISS construction, and NOP delays comparable.

The original latency CSV files are stored without manual modification under
`data/raw/week1/`. The current files contain:

| Dataset | Rows | HIT latency (min / median / mean / max) | MISS latency (min / median / mean / max) |
|---|---:|---:|---:|
| `calibration_rdtscp_50000.csv` | 50,000 | 90 / 100 / 99.33 / 156 | 824 / 1,094 / 1,117.76 / 2,630 |
| `calibration_rdtsc_50000.csv` | 50,000 | 70 / 78 / 78.08 / 980 | 838 / 1,068 / 1,082.08 / 2,518 |

The two archived datasets now have matched sample counts. MATLAB scripts and
histograms are stored under
`data/processed/week1/flush_reload_calibration/`. A written threshold decision
and repeatability analysis remain the next Week 1 items.

## Repository Layout

The repository keeps the stable top-level separation between source code,
automation, data, results, experiments, and reports. Weekly subdirectories are
created only when a week has artifacts, so the layout stays readable without
empty placeholder trees.

```text
cache-side-channel-project/
├── README.md
├── .gitignore
├── Makefile
├── src/
│   ├── week1/
│   │   ├── flush_reload_rdtscp.c
│   │   └── flush_reload_rdtsc.c
│   ├── flush_reload.c
│   ├── covert_sender.c
│   ├── covert_receiver.c
│   └── common.h
├── scripts/
├── data/
│   ├── raw/
│   │   └── week1/
│   │       ├── calibration_rdtscp_50000.csv
│   │       └── calibration_rdtsc_50000.csv
│   └── processed/
├── results/
│   ├── csv/
│   └── figures/
├── experiments/
│   ├── template.md
│   ├── week1/
│   │   ├── 2026-08-03-flush-reload-baseline.md
│   │   ├── 2026-08-04-single-thread-latency-calibration.md
│   │   └── 2026-08-05-threshold-calibration.md
│   └── week2/
│       └── 2026-08-08-covert-channel.md
├── journal/
│   ├── template.md
│   └── week1.md
├── report/
│   ├── week1.md
│   ├── week2.md
│   ├── week3.md
│   ├── week4.md
│   └── final-report.pdf
└── docs/
    └── repository-structure.md
```

For later work, add `weekN/` below `src/`, `data/raw/`,
`data/processed/`, `experiments/`, and `journal/` when that week produces
corresponding artifacts. Keep raw measurements separate from processed
summaries and presentation-ready results.

`experiments/weekN/` contains formal, reproducible records. It must include
successful, failed, partial, and inconclusive attempts when they tested a
defined hypothesis. `journal/weekN.md` is the chronological learning log for
problems, investigation steps, solutions, assumptions, and reflections.

## Experimental Platform

The project brief requires a native Linux installation rather than WSL or a
virtual machine because timing and MSR access are hardware-sensitive. Record
the exact values for the local run before publishing a final result.

- CPU model and microarchitecture: to be recorded
- Operating system and kernel: to be recorded
- Compiler and version: GCC, exact version to be recorded
- Main-thread CPU: logical CPU 5 in the current source
- CPU type: use a P-core on hybrid Intel systems
- TSC: invariant TSC assumed by the experiment
- Frequency governor, Turbo Boost, prefetchers, ASLR, and background load:
  record for each formal experiment

## Week 1 Reproduction

Build the two timing variants from the repository root on native Linux:

```bash
mkdir -p build
gcc -O3 -std=gnu11 -Wall -Wextra -march=native \
    -fno-pie -no-pie \
    src/week1/flush_reload_rdtscp.c \
    -o build/flush_reload_rdtscp

gcc -O3 -std=gnu11 -Wall -Wextra -march=native \
    -fno-pie -no-pie \
    src/week1/flush_reload_rdtsc.c \
    -o build/flush_reload_rdtsc
```

Run on the same P-core selected by `TARGET_CPU`:

```bash
sudo taskset -c 5 ./build/flush_reload_rdtscp > data/raw/week1/rdtscp_new.csv
sudo taskset -c 5 ./build/flush_reload_rdtsc > data/raw/week1/rdtsc_new.csv
```

Do not overwrite the archived raw CSV files. Use a new descriptive filename
for each additional run and record the command, commit, environment, and
parameters in an experiment note.

## Data and Analysis Rules

- Files under `data/raw/` are direct program output and must not be edited by
  hand.
- Files under `data/processed/` are generated from raw data by committed
  scripts.
- Files under `results/` are finalized tables or figures cited by reports.
- Every formal experiment records its source path, Git commit, hardware and
  software environment, compiler flags, CPU binding, raw-data path, analysis
  command, unexpected behavior, conclusion, and next step.

The Week 1 note at
`experiments/week1/2026-08-04-single-thread-latency-calibration.md` records the
current code organization and the observed latency distributions.

## Planned Analysis and Deliverables

1. Plot hit/miss latency histograms and select a defensible threshold.
2. Document timing controls and repeatability for the Week 1 check-in.
3. Use the validated probe in the Week 2 sender/receiver channel.
4. Measure error rate and capacity across same-core and cross-core layouts.
5. Prepare the Week 3 software T-table AES victim and validate recovered keys.
6. Add Week 4 controls, the technical report, the presentation, and a PQC
   leakage characterization.

## Limitations

Results are hardware- and OS-dependent. Scheduler activity, interrupts,
frequency changes, prefetchers, cache interference, TLB state, CPU migration,
and compiler choices can change measured latency. A timing separation in one
environment is not by itself evidence of portability or full key recovery.

## Research Notes

The full repository policy is documented in
`docs/repository-structure.md`. The Week 1 learning log
is in `journal/week1.md`; formal calibration details remain in
`experiments/week1/`.

## References

The project scope and weekly deliverables follow `intern-project-brief.pdf`.
The repository-structure document records the code/data/documentation and
experiment/journal separation used here.
