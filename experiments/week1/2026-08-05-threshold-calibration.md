# Threshold Calibration

## RDTSCP Calibration Results

To determine a system-specific threshold, we collected 50,000 RDTSCP-based measurements for both L1-cache hits and memory accesses. As shown in the figure, the L1 measurements are tightly concentrated around 100 TSC ticks, with 98.8% of the samples falling between 96 and 104 ticks. This value includes the overhead of the RDTSCP and fence instructions and therefore does not represent the latency of a single load alone. Memory-access timings are less constant: 92.5% of the samples fall between 1080 and 1120 ticks, while a small number extend beyond 1600 ticks. No L1 measurement exceeds 156 ticks, and no memory measurement is below 824 ticks, demonstrating a clear separation between the two distributions. Based on these results, we set the cache-hit threshold to **170 TSC ticks**. This follows the paper’s system-dependent calibration method while using the timing characteristics measured on our own platform.
