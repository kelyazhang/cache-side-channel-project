<style>
body {
  font-family: "Times New Roman", Times, serif;
}
</style>

# Week 1: Construction, Debugging, and Calibration of a Flush+Reload Measurement Primitive

## 1. Experimental Objectives

The objective of this week's work was to implement a reproducible and interpretable Flush+Reload measurement primitive on a Redwood Cove P-core of an Intel Core Ultra 7 155H processor. The experiment focused on the following tasks:

1. Map a shared, read-only, file-backed ordinary 4 KiB page using `mmap()`.
2. Select a fixed 64 B cache line within that page as the probe target.
3. Use `clflush` to create a MISS and one untimed load to create a HIT.
4. Measure reload latency using fenced `RDTSC` and `RDTSCP` instructions.
5. Investigate how a `clflush` at the end of the probe and the waiting interval before the probe affect the results.
6. Collect 50,000 rounds of RDTSC HIT/MISS data and 50,000 rounds of RDTSCP HIT/MISS data.
7. Use MATLAB to plot probability histograms, select classification thresholds, and calculate misclassification rates.

The project specification requires the official results to use `RDTSCP + LFENCE`. Therefore, the RDTSCP data serve as the official Week 1 calibration results, while the RDTSC data serve as a control experiment for analyzing timer overhead.

---

## 2. Experimental Environment and Measurement Definition

| Item | Configuration |
|---|---|
| Processor | Intel Core Ultra 7 155H |
| Microarchitecture | Meteor Lake, Redwood Cove P-core |
| Operating system | Ubuntu 24.04.4 LTS |
| Kernel | Linux 6.8.0-100-generic |
| Compiler | GCC 13.3.0 |
| Experimental CPU | CPU 5, P-core |
| Cache-line size | 64 B |
| SMT | Disabled |
| CPU isolation | CPUs 1-5 |
| Housekeeping CPUs | CPUs 0, 6, and 7 |
| CPU governor | `performance` |
| Turbo Boost | Disabled |
| Timing unit | Invariant TSC ticks |

The program is pinned to CPU 5 through `sched_setaffinity()`, and `taskset -c 5` is also used at runtime to prevent the thread from migrating to another P-core or an LP E-core. The main network IRQs are redirected to the housekeeping cores, and deep C-states are restricted to reduce noise caused by scheduling, interrupts, and changes in frequency states.

The unit reported in this experiment is **TSC ticks**, not core cycles at the core's current dynamic frequency. Intel's invariant TSC normally runs at an approximately fixed frequency, so one TSC tick cannot be interpreted directly as one actual core cycle.

---

## 3. Construction of the Measurement Primitive and Two Key Issues

### 3.1 Basic measurement approach

Flush+Reload requires two known states to be created deliberately before the reload latency of the same address is measured.

HIT sample:

```text
clflush(target)
-> wait for flush-related activity to stabilize
-> LOAD_ONCE(target), loading the target line back into the cache
-> wait for the load and line-fill state to stabilize
-> timed reload
```

MISS sample:

```text
clflush(target)
-> wait for flush-related activity to stabilize
-> do not access target
-> wait for the same amount of time
-> timed reload
```

The two paths are kept as identical as possible except for whether `LOAD_ONCE(target)` is executed. Consequently, the difference between a HIT and a MISS can be attributed unambiguously to whether the target cache line was accessed again before the actual probe.

### 3.2 Issue 1: Should another `clflush` be executed after the probe?

#### Initial consideration

In the early code, reload timing and `clflush` were placed in the same probe macro:

```asm
mfence
lfence
rdtscp
mov    r8d, eax
lfence
mov    reg, [target]
lfence
rdtscp
lfence
sub    eax, r8d
clflush [target]
```

At first, this design appeared to prepare the MISS state for the next round conveniently when the current probe ended. Because `clflush` was placed after the second timestamp, it was not directly included in the latency of the current sample.

#### Problems in practice

Although the trailing `clflush` was outside the current timing interval, it changed the cache state after the current probe and introduced coupling with the next round:

1. The probe was no longer a pure observation operation; it actively modified state at its end.
2. The next round executed `clflush` again at its beginning, causing two consecutive flushes of the same address.
3. The trailing flush, the initial flush of the next round, and the next preparatory load could interfere with one another along microarchitectural paths.
4. If the next round produced an abnormal miss, its exact source was difficult to determine.
5. Samples were no longer independent, increasing coherence-invalidation traffic and cross-round correlation.

The fences already present at the end of the probe could not guarantee that the trailing `clflush` had completed fully before `LOAD_ONCE` in the next round began, because the flush itself appeared after the final fence. Constraining it would require a new completion boundary after the flush.

#### Final decision

The final implementation removes `clflush` from the end of the probe and completely separates state preparation from the actual measurement:

```c
FLUSH_TARGET(target);
PROBE_RELOAD_ONLY(target, elapsed);
```

`FLUSH_TARGET` explicitly prepares the state for the current round:

```c
#define FLUSH_TARGET(address) do {
    __asm__ volatile(
        "clflush (%0)\n\t"
        "mfence\n\t"
        :
        : "r"(address)
        : "memory"
    );
} while (0)
```

The actual probe only reads timestamps and loads the target address. It does not modify the cache state for any subsequent sample.

#### Why this produces more reliable data

After the correction, every sample follows an independent structure:

```text
explicitly prepare the cache state for the current round
-> probe the current round
-> save the result of the current round
```

A HIT has only one cause: `LOAD_ONCE` was executed after the flush in the current round. A MISS also has only one cause: the target address was not accessed after the flush in the current round. This design reduces repeated invalidation requests, pressure on the cache-line fill path, and coupling between samples, giving every result a clearly defined state origin.

### 3.3 Issue 2: Is a NOP waiting interval required before the actual probe?

#### Observation without a waiting interval

After removing `clflush` from the end of the probe, the initial HIT path was:

```text
clflush
-> mfence
-> untimed load
-> immediately begin the timed reload
```

This version was run for only 100 rounds as a quick debugging test. Its HIT latency was mainly concentrated between 130 and 144 TSC ticks:

| Metric | RDTSCP HIT without a waiting interval |
|---|---:|
| Number of samples | 100 |
| Mean | 137.42 ticks |
| Median | 138 ticks |
| Standard deviation | 4.14 ticks |
| Minimum | 128 ticks |
| Maximum | 146 ticks |

These results were clearly higher than the previously stable L1 baseline. Because the actual timing assembly had not changed, the more likely cause was an interval that was too short between the preparatory load and the timed reload.

This 100-round dataset documents only how the problem was discovered; it is not part of the formal calibration results.

#### Microarchitectural analysis

After the following instructions are executed:

```asm
clflush [target]
mfence
mov rax, [target]
```

the preparatory load initiates a new demand read. The data may come from a lower-level cache or memory, pass through the line-fill buffer and related fill paths, and then be allocated in L1D.

If the second, timed load is issued immediately, the preceding load may have completed architecturally while the following states have only just ended or remain in a brief recovery phase:

1. Data from the preceding demand miss has only just returned.
2. The line-fill buffer and fill path have not completely returned to a stable state.
3. The cache line is already available to the load, but L1D allocation and related internal resources still have transient effects.
4. The preceding `clflush`, `mfence`, and demand miss are still affecting the load queue, fill buffers, or pipeline resources.
5. The timestamp instructions are too close to the preceding high-latency event, so the timing framework is also affected by resource state.

Therefore, a latency of approximately 138 ticks indicates only that the probe had not yet exhibited the stable L1 baseline. Its absolute value alone is insufficient to conclude that the access must have been an L2 or LLC hit.

#### Final decision

The final experiment introduces three user-space NOP busy-wait intervals:

```c
#define FLUSH_SETTLE_NOPS   5000U
#define RELOAD_SETTLE_NOPS  5000U
#define ROUND_GAP_NOPS     20000U
```

The resulting path is:

```text
clflush
-> mfence
-> 5000 NOP iterations to let flush-related activity stabilize
-> execute LOAD_ONCE for a HIT, or do not access target for a MISS
-> 5000 NOP iterations to let the load/fill state stabilize
-> timed reload
-> 20000 NOP iterations after one HIT/MISS pair
```

The busy-wait function is:

```c
static __attribute__((always_inline)) inline void
busy_wait_nops(uint32_t iterations)
{
    while (iterations != 0U) {
        __asm__ volatile(
            "nop\n\t"
            :
            :
            : "memory"
        );

        iterations--;
    }
}
```

The experiment does not use `sleep()`, `usleep()`, `nanosleep()`, or `sched_yield()`. These calls may cause the thread to enter the kernel, block, or voluntarily yield the CPU, introducing context switches, scheduler interference, changes in cache or TLB state, idle-state transitions, and frequency changes.

The NOP loop always runs on the pinned P-core and is placed outside the two timestamps of the actual measurement. It is therefore not included directly in the reload latency.

#### Why the NOP intervals improve the results

After the waiting intervals were added, the median HIT latencies in the formal 50,000-round experiments decreased to:

```text
Median RDTSCP HIT = 100 ticks
Median RDTSC HIT  = 78 ticks
```

Both values are substantially lower than the median of approximately 138 ticks without a waiting interval. This improvement does not mean that NOPs actively move data from L2 to L1. Instead:

1. `LOAD_ONCE(target)` has already initiated the data read and cache-line fill.
2. The NOPs delay the actual probe.
3. The timed reload is more likely to observe an L1 cache line whose fill has completed and whose residency has stabilized.
4. The flush, miss, and line fill are separated sufficiently in physical time from the actual timing interval.
5. Continuous NOP execution may also reduce perturbations caused by idle states, recovery from clock gating, and changes in front-end state.

`LFENCE` already provides architectural ordering, but architectural completion of a load does not necessarily mean that every fill path and internal resource has stabilized at the same moment. The NOP experiment therefore reflects the effects of microarchitectural transients and resource state; it does not imply that the original code violated the ordering rules of the fence.

### 3.4 Final probe structure

The timing path of the official RDTSCP version is:

```asm
lfence
rdtscp
mov    r8d, eax
lfence
mov    reg, [target]
lfence
rdtscp
lfence
sub    eax, r8d
```

The corresponding macro is responsible only for timing and loading the target:

```c
#define PROBE_RELOAD_ONLY(address, elapsed) do {
    uint64_t loaded_value__;

    __asm__ volatile(
        "lfence\n\t"
        "rdtscp\n\t"
        "movl %%eax, %%r8d\n\t"
        "lfence\n\t"
        "movq (%2), %1\n\t"
        "lfence\n\t"
        "rdtscp\n\t"
        "lfence\n\t"
        "subl %%r8d, %%eax\n\t"
        : "=&a"(elapsed), "=&r"(loaded_value__)
        : "r"(address)
        : "rdx", "rcx", "r8", "cc", "memory"
    );

    (void)loaded_value__;
} while (0)
```

The final principle can be summarized as follows: **state preparation belongs at the beginning of the current sample, the probe is responsible only for observation, and suitable waiting intervals reduce coupling between samples.**

---

## 4. Comparison of the RDTSC and RDTSCP Timing Methods

### 4.1 Similarities

Both `RDTSC` and `RDTSCP` read IA32_TSC and return values in TSC ticks:

```text
EDX = upper 32 bits of the TSC
EAX = lower 32 bits of the TSC
```

Each individual measurement is much shorter than `2^32` ticks, so the hot path saves and subtracts only the lower 32 bits:

```asm
mov r8d, eax
...
sub eax, r8d
```

### 4.2 RDTSC

`RDTSC` is not itself a fully serializing instruction. Therefore, the following sequence cannot be used directly:

```asm
rdtsc
load
rdtsc
```

This experiment uses `LFENCE` to establish the measurement boundaries:

```asm
lfence
rdtsc
mov    r8d, eax
lfence
mov    reg, [target]
lfence
rdtsc
lfence
sub    eax, r8d
```

RDTSC is relatively lightweight. When the thread is strictly pinned to a core and fences are used correctly, it introduces less perturbation when measuring a single short load and is therefore suitable for the microbenchmark control experiment.

### 4.3 RDTSCP

In addition to reading the TSC, `RDTSCP` reads:

```text
IA32_TSC_AUX -> ECX
```

Linux normally configures TSC_AUX with an identifier associated with the logical CPU. It can therefore help determine whether CPU migration occurred during a measurement.

RDTSCP has stronger ordering semantics for preceding instructions and loads than RDTSC. However, it does not guarantee that preceding stores are globally visible, nor can it completely prevent subsequent instructions from starting early. It is therefore still followed by an `LFENCE`.

### 4.4 Why the absolute results of the two timers differ

Except for the timestamp instructions, the two formal versions use the same mmap target, CPU affinity, flush operation, fences, NOP parameters, preparatory load, and compiler optimization level.

The 50,000-round results are:

```text
Median RDTSC HIT  = 78 ticks
Median RDTSCP HIT = 100 ticks
Difference        = 22 ticks
```

RDTSCP must also read TSC_AUX, write ECX, and follow a different internal execution and ordering path, so its timing framework is heavier. The 22-tick difference cannot be attributed solely to writing ECX, nor does it indicate that RDTSCP measured an L1 hit as an L2 or LLC hit.

The final use of each timer is:

| Use case | Method |
|---|---|
| Official Week 1 calibration results | `RDTSCP + LFENCE` |
| Control experiment for additional timer overhead | `LFENCE + RDTSC` |
| Auxiliary CPU migration detection | RDTSCP with TSC_AUX checking |

Because the two timing templates have different fixed overheads, their absolute latency values and classification thresholds must not be mixed.

---

## 5. Formal 50,000-Round Calibration Results

Each timing method produces one CSV file:

```text
calibration_rdtsc_50000.csv
calibration_rdtscp_50000.csv
```

Each file contains one header row and 50,000 sample rows:

```text
index,hit_ticks,miss_ticks
```

Each row records one HIT sample and one MISS sample from the same round.

### 5.1 RDTSC results

| Metric | HIT | MISS |
|:--|:---|:---|
| Number of samples | 50,000 | 50,000 |
| Mean | 78.0780 | 1082.0814 |
| Median | 78 | 1068 |
| Standard deviation | 4.1494 | 90.1107 |
| Minimum | 70 | 838 |
| 1st percentile | 74 | 974 |
| 5th percentile | 78 | 1058 |
| 25th percentile | 78 | 1064 |
| 75th percentile | 78 | 1074 |
| 95th percentile | 78 | 1086 |
| 99th percentile | 82 | 1600 |
| Maximum | 980 | 2518 |

The HIT distribution is highly concentrated: 47,114 samples are exactly 78 ticks, accounting for 94.228% of all HITs. The data contain only one anomalous 980-tick HIT. It raises the maximum and standard deviation but does not represent a change in the main distribution.

The main MISS peak is approximately 1060-1080 ticks, with both the median and mode at 1068 ticks. The 99th percentile rises to 1600 ticks, showing that the miss distribution has a long tail. Therefore, the median is more robust than the mean when describing typical miss latency.

RDTSC uses the following threshold:

```matlab
thresholdTicks = 150;
```

The classification results are:

| Metric | Result |
|---|:---|
| False misses | 1 / 50,000, or 0.002000% |
| False hits | 0 / 50,000, or 0.000000% |
| Total misclassifications | 1 / 100,000, or 0.001000% |

The only false miss is the anomalous 980-tick HIT. The 150-tick threshold remains far below the minimum MISS of 838 ticks, so it produces no false hits.

### 5.2 RDTSCP results

| Metric | HIT | MISS |
|:--|:---|:---|
| Number of samples | 50,000 | 50,000 |
| Mean | 99.3319 | 1117.7617 |
| Median | 100 | 1094 |
| Standard deviation | 2.2736 | 111.5344 |
| Minimum | 90 | 824 |
| 1st percentile | 96 | 1010 |
| 5th percentile | 96 | 1082 |
| 25th percentile | 98 | 1090 |
| 75th percentile | 100 | 1098 |
| 95th percentile | 102 | 1422 |
| 99th percentile | 104 | 1628 |
| Maximum | 156 | 2630 |

The main RDTSCP HIT distribution is concentrated between 98 and 102 ticks, containing 46,160 samples, or 92.32% of all HITs. The maximum HIT is 156 ticks, leaving a large gap to the minimum MISS of 824 ticks.

The MISS median is 1094 ticks, and the main peak lies at approximately 1086-1104 ticks. This distribution also has a long tail.

RDTSCP uses the following threshold:

```matlab
thresholdTicks = 170;
```

The classification results are:

| Metric | Result |
|---|:---|
| False misses | 0 / 50,000, or 0.000000% |
| False hits | 0 / 50,000, or 0.000000% |
| Total misclassifications | 0 / 100,000, or 0.000000% |

The 170-tick threshold lies within the wide empty region between the maximum HIT of 156 ticks and the minimum MISS of 824 ticks.

### 5.3 Comparison of the two timing methods

| Metric | RDTSC | RDTSCP | RDTSCP - RDTSC |
|---|:---|:---|:---|
| Mean HIT | 78.0780 | 99.3319 | +21.2539 |
| Median HIT | 78 | 100 | +22 |
| Mean MISS | 1082.0814 | 1117.7617 | +35.6803 |
| Median MISS | 1068 | 1094 | +26 |
| Main HIT peak | 78 | 98-100 | Approximately +20-22 |
| Main MISS peak | 1068 | 1092 | +24 |
| Classification threshold | 150 | 170 | +20 |

The difference between the HIT medians is consistent with the fixed overhead difference between the two timers. The difference between the MISS means is larger, mainly because the miss data have long tails. Timer overhead should therefore be compared using the main HIT peak or median rather than only the MISS mean.

---

## 6. MATLAB Data Processing and Figure Interpretation

MATLAB R2022a is used to process the two CSV files. The corresponding scripts are:

```text
plot_calibration_rdtsc.m
plot_calibration_rdtscp.m
```

The scripts read the data with `readtable()` and check for the `hit_ticks` and `miss_ticks` columns:

```matlab
T = readtable(csvFile);
requiredColumns = {'hit_ticks', 'miss_ticks'};

hitTicks  = double(T.hit_ticks);
missTicks = double(T.miss_ticks);

hitTicks  = hitTicks(isfinite(hitTicks));
missTicks = missTicks(isfinite(missTicks));
```

The misclassification rates are calculated as follows:

```matlab
falseMissRate = mean(hitTicks >= thresholdTicks);
falseHitRate  = mean(missTicks < thresholdTicks);
```

The probability histograms use 2-tick bins and probability normalization:

```matlab
binWidth = 2;

hitProbability = histcounts(
    hitTicks,
    binEdges,
    'Normalization',
    'probability'
);

missProbability = histcounts(
    missTicks,
    binEdges,
    'Normalization',
    'probability'
);
```

In the figures, green represents From L1 Cache, blue represents From Memory, and the dashed line represents the selected threshold. The horizontal axis is `Probe Time (TSC ticks)`, and the vertical axis is `Fraction of Samples`.

The following images can be inserted into the Markdown file:

```markdown
![RDTSC calibration](calibration_rdtsc_histogram.png)

![RDTSCP calibration](calibration_rdtscp_histogram.png)
```

Both figures show a clear bimodal distribution. The narrow peak on the left represents cache hits, while the broader peak on the right represents memory-side reloads after `clflush`. A wide empty region of several hundred ticks separates the two main distributions, demonstrating that the measurement primitive can reliably distinguish HITs from MISSes.

---

## 7. Reproducing the Experiment on the Experimental Machine

The following procedure includes only the operations required on the experimental machine.

### 7.1 Compiling the official RDTSCP version

Confirm the sample count:

```bash
sed -i \
    's/^#define SAMPLE_COUNT .*/#define SAMPLE_COUNT 50000/' \
    flush_reload.c
```

Compile the program:

```bash
gcc -O3 -std=gnu11 -Wall -Wextra -march=native \
    -fno-pie -no-pie \
    flush_reload.c -o week1_probe_rdtscp
```

Inspect the timing path in the disassembly:

```bash
objdump -d -Mintel --no-show-raw-insn \
    week1_probe_rdtscp > disasm_rdtscp.txt

grep -n -C 12 "rdtscp" disasm_rdtscp.txt
```

Run the program on CPU 5 and save the data:

```bash
sudo taskset -c 5 ./week1_probe_rdtscp \
    > calibration_rdtscp_50000.csv
```

Check the line count and record the checksum:

```bash
wc -l calibration_rdtscp_50000.csv
sha256sum calibration_rdtscp_50000.csv
```

The expected line count is 50,001: one header row and 50,000 sample rows.

### 7.2 Compiling the RDTSC control version

Confirm the sample count:

```bash
sed -i \
    's/^#define SAMPLE_COUNT .*/#define SAMPLE_COUNT 50000/' \
    flush_reload_rdtsc.c
```

Compile the program:

```bash
gcc -O3 -std=gnu11 -Wall -Wextra -march=native \
    -fno-pie -no-pie \
    flush_reload_rdtsc.c -o week1_probe_rdtsc
```

Inspect the timing path in the disassembly:

```bash
objdump -d -Mintel --no-show-raw-insn \
    week1_probe_rdtsc > disasm_rdtsc.txt

grep -n -C 12 "rdtsc" disasm_rdtsc.txt
```

Run the program on CPU 5 and save the data:

```bash
sudo taskset -c 5 ./week1_probe_rdtsc \
    > calibration_rdtsc_50000.csv
```

Check the line count and record the checksum:

```bash
wc -l calibration_rdtsc_50000.csv
sha256sum calibration_rdtsc_50000.csv
```

The expected line count is also 50,001.

---

## 8. Lessons Learned

### 8.1 The probe should be responsible only for observation

Hiding `clflush` at the end of the probe modifies the state of the next round and introduces coupling between samples. A clearer design explicitly prepares the cache state at the beginning of every round, while the probe itself performs only timestamp readings and the target load.

### 8.2 Fences solve ordering problems; waiting intervals isolate microarchitectural transients

`LFENCE` establishes architectural timing boundaries, but it does not guarantee that all line-fill paths and internal resources return to a stable state at the exact moment when a load completes. Suitable NOP intervals separate the flush, miss, and fill from the actual timing interval, making HIT measurements more consistent with the stable L1 baseline.

### 8.3 Timer overhead is part of the measurement result

RDTSC and RDTSCP read the same TSC, but they have different ordering semantics and internal overhead. The median RDTSCP HIT is 22 ticks higher than the median RDTSC HIT, mainly because of the timing framework rather than a change in cache level. Thresholds must therefore be calibrated independently for each timing template.

### 8.4 The final primitive achieves stable classification

The formal results are:

```text
RDTSC:
Median HIT                  = 78 ticks
Median MISS                 = 1068 ticks
Threshold                   = 150 ticks
Total misclassification rate = 0.001%

RDTSCP:
Median HIT                  = 100 ticks
Median MISS                 = 1094 ticks
Threshold                   = 170 ticks
Total misclassification rate = 0%
```

The main HIT and MISS distributions are clearly separated. The current Flush+Reload primitive therefore satisfies the Week 1 requirements for reproducible measurement, a clear bimodal distribution, and interpretable thresholds.

---

## 9. Final File List

```text
Source code:
    flush_reload.c
    flush_reload_rdtsc.c

Raw data:
    calibration_rdtscp_50000.csv
    calibration_rdtsc_50000.csv

MATLAB:
    plot_calibration_rdtscp.m
    plot_calibration_rdtsc.m

Figures:
    calibration_rdtscp_histogram.png
    calibration_rdtscp_histogram.pdf
    calibration_rdtscp_histogram.fig

    calibration_rdtsc_histogram.png
    calibration_rdtsc_histogram.pdf
    calibration_rdtsc_histogram.fig
```
