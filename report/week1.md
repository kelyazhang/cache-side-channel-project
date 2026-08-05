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
- Added MATLAB plotting scripts and hit/miss histograms under
  `data/processed/week1/flush_reload_calibration/`.
- Recorded the organization and initial statistics in
  `experiments/week1/2026-08-04-single-thread-latency-calibration.md`.

## Initial Raw Results

| Dataset | Rows | HIT mean | MISS mean |
|---|---:|---:|---:|
| `calibration_rdtscp_50000.csv` | 50,000 | 99.33 ticks | 1,117.76 ticks |
| `calibration_rdtsc_50000.csv` | 50,000 | 78.08 ticks | 1,082.08 ticks |

The observed central latencies are clearly separated, and the datasets now
have matched sample counts. Histogram-based threshold selection and repeated
environment checks are still required.

## Remaining Week 1 Work

1. Reconcile the threshold choice across the RDTSCP and RDTSC variants.
2. Document the controlled noise sources and repeatability checks.
3. Prepare the Week 1 check-in before implementing the Week 2 channel.
