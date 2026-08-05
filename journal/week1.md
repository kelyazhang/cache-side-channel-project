# Week 1 Learning Journal

## 2026-08-04 - Organize the Calibration Baseline

### Context

The single-thread Flush+Reload calibration was completed in two timing
variants: one using `RDTSCP` and one using `RDTSC`. The source files and raw
latency CSV files needed to be made easy to review and reproduce.

### Observation or Problem

The older repository layout mixed generic experiment records with the weekly
project progression. It also had no dedicated place for the reasoning behind
parameter choices, failed attempts, or debugging notes.

### Investigation

The project brief was compared with the current repository. Week 1 was mapped
to the trusted Flush+Reload primitive, while Weeks 2-4 were mapped to the
covert channel, AES recovery, and rigor/PQC work. The two archived CSV files
were checked without editing their contents. The `RDTSC` file contains 100
data rows despite its `50000` filename; the observed file contents were kept
as authoritative.

### Resolution or Current Understanding

The repository now stores weekly source and raw data under `week1/`, while
formal experiment records remain under `experiments/week1/`. A separate
`journal/week1.md` is used for chronological learning and reflection. Formal
experiment records will include successful, failed, partial, and inconclusive
attempts rather than hiding failures.

### Reflection and Next Action

The central HIT and MISS latency values are well separated, but the sample
counts are not matched. The next action is to generate matched-sample
histograms, choose a threshold, and document the noise controls before
starting the Week 2 covert channel.

## 2026-08-05 - Journal Structure Decision

The learning log is intentionally separate from `experiments/`. This keeps
short-lived debugging thoughts and reflections chronological without weakening
the reproducibility of formal experiment records. Each future weekly journal
will link to the relevant experiment record, commit, data file, and figure.
