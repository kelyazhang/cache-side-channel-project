# Cache Side-Channel Project

This repository contains a reproducible Week 1 study of a Flush+Reload cache
timing primitive on an Intel/Linux system. It preserves the measurement code,
raw timing data, analysis scripts, calibration figures, experiment notes, and
the final Week 1 report in one public research record.

The repository is intentionally trimmed to artifacts that currently exist.
Placeholder files for later weeks and superseded source, script, journal, and
report files have been removed. Future work will be added only when it produces
real code, data, or documentation.

## Current Status

Week 1 is complete. Two controlled probe variants were evaluated:

- **RDTSCP + LFENCE** is the formal measurement configuration.
- **RDTSC + LFENCE** is retained as a control for timestamp-instruction
  overhead.

Both variants use the same file-backed shared mapping, target cache line, CPU
affinity, cache-state construction, settling delays, and buffered output. The
timestamp instruction is the principal controlled difference.

### Calibration Results

| Probe | Hit median | Miss median | Threshold | Classification result |
|---|---:|---:|---:|---|
| RDTSCP formal probe | 100 TSC ticks | 1,094 TSC ticks | **170 ticks** | 0 false misses and 0 false hits in 100,000 classified samples |
| RDTSC control probe | 78 TSC ticks | 1,068 TSC ticks | **150 ticks** | 1 false miss and 0 false hits in 100,000 classified samples |

The formal Week 1 cache-hit rule is therefore:

```text
reload time < 170 TSC ticks  -> cache hit
reload time >= 170 TSC ticks -> cache miss
```

This threshold is specific to the measured platform and probe sequence. It
must be recalibrated when the CPU, kernel configuration, timer sequence, CPU
affinity, compiler settings, or frequency policy changes.

## Repository Layout

The following tree matches the current tracked repository contents:

```text
cache-side-channel-project/
|-- .gitignore
|-- Makefile
|-- README.md
|-- src/
|   `-- week1/
|       |-- flush_reload_rdtsc.c
|       `-- flush_reload_rdtscp.c
|-- data/
|   |-- raw/
|   |   |-- .gitkeep
|   |   `-- week1/
|   |       |-- calibration_rdtsc_50000.csv
|   |       `-- calibration_rdtscp_50000.csv
|   `-- processed/
|       |-- .gitkeep
|       `-- week1/
|           `-- flush_reload_calibration/
|               |-- flush_reload_rdtsc_calibration.png
|               |-- flush_reload_rdtscp_calibration.png
|               |-- plot_calibration_rdtsc.m
|               `-- plot_calibration_rdtscp.m
|-- experiments/
|   |-- template.md
|   `-- week1/
|       |-- 2026-08-03-flush-reload-baseline.md
|       |-- 2026-08-04-single-thread-latency-calibration.md
|       `-- 2026-08-05-threshold-calibration.md
|-- report/
|   |-- report_week1.md
|   `-- report_week1.pdf
`-- docs/
    |-- repository-structure.md
    `-- week1_journal/
        `-- Experimental Environment_week1.md
```

`Makefile` is currently reserved for future build automation. The documented
GCC commands below are the current build entry point.

## Artifact Guide

### Source Code

- [`src/week1/flush_reload_rdtscp.c`](src/week1/flush_reload_rdtscp.c) contains
  the formal RDTSCP probe with LFENCE ordering.
- [`src/week1/flush_reload_rdtsc.c`](src/week1/flush_reload_rdtsc.c) contains
  the controlled RDTSC variant.

Each program pins itself to logical CPU 5, maps a shared read-only page from
`/bin/ls`, constructs independent HIT and MISS samples, and writes the timing
results only after measurement has finished.

### Raw and Processed Data

- [`data/raw/week1/`](data/raw/week1/) contains the two archived 50,000-round
  CSV datasets. These files are direct experimental output and should not be
  edited manually.
- [`data/processed/week1/flush_reload_calibration/`](data/processed/week1/flush_reload_calibration/)
  contains the MATLAB plotting scripts and generated calibration histograms.

### Experiment Records and Report

- [`experiments/week1/`](experiments/week1/) contains dated notes for the
  baseline, latency calibration, and threshold decision.
- [`report/report_week1.md`](report/report_week1.md) is the complete Week 1
  technical report.
- [`report/report_week1.pdf`](report/report_week1.pdf) is the publication-ready
  PDF version of the report.
- [`docs/repository-structure.md`](docs/repository-structure.md) records the
  broader repository organization and research-record conventions.
- [`docs/week1_journal/Experimental Environment_week1.md`](docs/week1_journal/Experimental%20Environment_week1.md)
  records the Week 1 host, kernel, compiler, CPU topology, boot parameters,
  runtime controls, and verification commands.

## Measurement Procedure

Each recorded sample follows one of two paths:

```text
flush target with CLFLUSH
-> MFENCE
-> wait for the flush to settle
-> HIT: perform one untimed target load
   MISS: do not access the target
-> wait for the reload state to settle
-> time one target reload
-> store the result in memory
```

The formal timer surrounds the target load with LFENCE and RDTSCP operations.
Formatted output and file I/O occur after the sampling loop so they do not
contaminate the timed interval.

## Build and Run

### Requirements

- Native x86-64 Linux
- GCC with GNU C11 support
- A processor supporting `RDTSC`, `RDTSCP`, `CLFLUSH`, and `LFENCE`
- Permission to set CPU affinity

Build both variants from the repository root:

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

The source currently selects logical CPU 5 through `TARGET_CPU`. Review
`TARGET_CPU` and `SAMPLE_COUNT` in each source file before running on another
machine. The CPU passed to `taskset` must match the value compiled into the
program.

Run the probes and save each new dataset under a new descriptive filename:

```bash
sudo taskset -c 5 ./build/flush_reload_rdtscp \
    > data/raw/week1/rdtscp_new.csv

sudo taskset -c 5 ./build/flush_reload_rdtsc \
    > data/raw/week1/rdtsc_new.csv
```

Do not overwrite the archived calibration CSV files. A formal rerun should
also record the Git commit, hardware and software environment, compiler flags,
CPU binding, kernel configuration, and analysis command.

## Experimental Platform

The reported Week 1 calibration used:

- Intel Core Ultra 7 155H (Meteor Lake)
- one Redwood Cove performance core
- logical CPU 5
- Ubuntu 24.04.4 LTS with Linux kernel 6.8.0-100-generic
- GCC 13.3.0
- native Linux with the measurement process pinned to the selected core
- invariant TSC timing
- SMT disabled and measurement cores isolated from ordinary housekeeping work
- performance-oriented frequency and idle-state controls

See the [Week 1 environment record](docs/week1_journal/Experimental%20Environment_week1.md)
for the complete host configuration and verification procedure. The
[Week 1 report](report/report_week1.md) documents the scheduler, interrupt,
idle-state, cache-state, and timestamping controls used for calibration.

## Data Integrity Rules

- Treat files under `data/raw/` as immutable experimental output.
- Generate plots and summaries from committed analysis scripts.
- Keep build products in `build/`; compiled binaries are not research
  artifacts and should not be committed.
- Add a new weekly directory only when that week has actual artifacts.
- Record unsuccessful and inconclusive experiments when they test a defined
  hypothesis; reproducibility includes failures as well as successful runs.

## Scope and Limitations

This repository demonstrates a calibrated single-process Flush+Reload timing
primitive. It does not yet contain a two-process covert channel, AES key
recovery, Prime+Probe implementation, or PQC leakage experiment.

Cache timing is sensitive to CPU topology, scheduling, interrupts, frequency
changes, prefetchers, cache interference, TLB state, compiler choices, and
kernel configuration. Clear separation on this machine does not establish a
universal threshold or prove portability to another system.

## Planned Next Work

Future artifacts may extend the validated primitive toward:

1. a synchronized two-process Flush+Reload covert channel;
2. error-rate and channel-capacity measurements across CPU placements;
3. a software T-table AES key-recovery experiment;
4. Prime+Probe and eviction-set construction; and
5. a small post-quantum or larger-system cache-leakage study.

These items are roadmap goals only. They will appear in the repository when
corresponding code, measurements, and experiment records are available.
