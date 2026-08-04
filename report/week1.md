# Week 1 Report: Flush+Reload Calibration

## Objective

Build a reliable single-thread Flush+Reload timing primitive before moving to
the Week 2 covert channel.

## Completed Work

- Preserved `RDTSCP` and `RDTSC` timing variants as a controlled comparison.
- Kept the shared read-only mapping, target cache line, CPU affinity, HIT/MISS
  construction, warm-up, and NOP delays explicit.
- Organized source code under `src/week1/` and raw latency data under
  `data/raw/week1/`.
- Recorded the organization and initial statistics in
  `experiments/week1/2026-08-04-single-thread-latency-calibration.md`.

## Initial Raw Results

| Dataset | Rows | HIT mean | MISS mean |
|---|---:|---:|---:|
| `calibration_rdtscp_50000.csv` | 50,000 | 99.33 ticks | 1,117.76 ticks |
| `calibration_rdtsc_50000.csv` | 100 | 78.06 ticks | 1,021.94 ticks |

The observed central latencies are clearly separated. The two files have
different sample counts, so this is an initial calibration result rather than
a matched statistical comparison.

## Remaining Week 1 Work

1. Generate and commit hit/miss latency histograms.
2. Select a threshold and document the decision and controlled noise sources.
3. Repeat both timing variants with matched sample counts.
4. Prepare the Week 1 check-in before implementing the Week 2 channel.
