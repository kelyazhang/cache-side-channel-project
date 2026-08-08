# L1 Cache-Hit Baseline Before the Flush+Reload Experiments

## Basic Information

- Date: 2026-08-03
- Experiment ID: `week1-l1-cache-hit-baseline`
- Platform: Intel Core Ultra 7 155H, Redwood Cove P-core
- Timing unit: invariant TSC ticks
- Purpose: preliminary measurement before the formal Flush+Reload experiments
- Archived artifacts: no standalone source file or raw dataset was preserved for this preparatory test

## Objective

After configuring the experimental machine, the first step was to establish an
approximate L1 cache-hit baseline for this processor and timing environment.
This value provides a reference for interpreting the HIT measurements in the
subsequent Flush+Reload calibration experiments.

## Method

A fixed 64-bit word was selected within one 64-byte cache line. The address was
read once without timing so that the line became resident in the cache. The
same address was then loaded again inside a fenced timestamp interval. No
`clflush` was executed between the preparatory load and the timed load, so the
second access represented the most direct cache-hit path used in this study.

The essential operation was equivalent to the following sequence:

```asm
; Bring the target cache line into the cache
mov    rax, [target]

; Measure a second load from the same address
lfence
rdtsc
mov    r8d, eax
lfence
mov    rax, [target]
lfence
rdtsc
lfence
sub    eax, r8d
```

The test was run on the designated experimental P-core after CPU affinity,
frequency, interrupt, and power-management controls had been configured. The
same address was read repeatedly to confirm the approximate range rather than
to produce a formal statistical dataset.

## Observed Baseline

The direct repeated-load measurements were typically approximately:

```text
70-80 TSC ticks
```

This range is the measured timing-template baseline, not the latency of the
load instruction alone. It includes the overhead of the timestamp instructions,
fences, register operations, and the target load.

## Interpretation and Role in Later Experiments

The 70-80-tick result established the expected scale of a stable L1-resident
access under the RDTSC-based timing path. It was later consistent with the
formal RDTSC calibration, whose HIT distribution had a median and main peak of
78 ticks.

This preliminary baseline also provided a useful debugging reference. A later
RDTSCP test without a sufficient waiting interval reported HIT values near 138
ticks, which were clearly above this stable baseline. That difference motivated
the investigation of timer overhead and the microarchitectural state between
the preparatory load and the timed probe.

The formal experiments that follow use archived source code, 50,000-round raw
datasets, and independently calibrated thresholds. This note records only the
initial baseline measurement that guided those experiments.
