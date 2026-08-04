#define _GNU_SOURCE

/*
 * Flush+Reload calibration experiment using RDTSC for timestamping.
 *
 * This is a controlled counterpart to the RDTSCP version: mapping, target,
 * CPU affinity, HIT/MISS construction, and all NOP delays remain unchanged.
 * Only the timestamp instruction differs.
 *
 * HIT sequence:
 *     clflush(target), mfence, settle, load target, settle, timed reload
 * MISS sequence:
 *     clflush(target), mfence, settle, settle, timed reload
 *
 * The probe subtracts the low 32 bits of two RDTSC readings and reports the
 * result in invariant TSC ticks. Each measurement is far shorter than the
 * 32-bit wraparound interval. Delays and I/O remain outside the timed region,
 * and the probe does not flush the line after measurement.
 *
 * Build:
 *     gcc -O3 -std=gnu11 -Wall -Wextra -march=native \
 *         -fno-pie -no-pie \
 *         flush_reload_rdtsc.c -o week1_probe
 *
 * Run:
 *     sudo taskset -c 5 ./week1_probe
 */

#include <fcntl.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>

/* Logical CPU used by the experiment. Match this value in taskset. */
#define TARGET_CPU 5

/* Number of recorded rounds; each round contains one HIT and one MISS. */
#define SAMPLE_COUNT 100

/*
 * Warm-up rounds used to establish page-table, TLB, cache-operation, and
 * timestamping state before samples are recorded.
 */
#define WARMUP_ROUNDS 1000

/* NOP iterations separating clflush + mfence from state preparation. */
#define FLUSH_SETTLE_NOPS 5000U

/*
 * NOP iterations between the HIT preparation load and the probe. The MISS
 * path applies the same delay without accessing the target.
 */
#define RELOAD_SETTLE_NOPS 5000U

/*
 * NOP iterations between rounds, limiting carry-over from cache fills,
 * fences, and other state created by the preceding round.
 */
#define ROUND_GAP_NOPS 20000U

/* Shared read-only file that backs the target mapping. */
#define TARGET_FILE "/bin/ls"

/* Cache-line offset within the mapped page. */
#define TARGET_OFFSET 0

/*
 * Buffer samples in memory and defer formatted I/O until measurement ends.
 */
static uint32_t hit_ticks[SAMPLE_COUNT];
static uint32_t miss_ticks[SAMPLE_COUNT];

/*
 * Pin the process to one logical CPU to prevent migration and preserve private
 * cache, TLB, and core-local microarchitectural state.
 */
static int bind_to_cpu(int cpu_id)
{
    cpu_set_t set;

    CPU_ZERO(&set);
    CPU_SET(cpu_id, &set);

    if (sched_setaffinity(0, sizeof(set), &set) != 0) {
        perror("sched_setaffinity");
        return -1;
    }

    return 0;
}

/*
 * Busy-wait without sleeping, blocking, or voluntarily yielding the CPU.
 * The loop-control instructions remain outside the timed RDTSC interval.
 */
static __attribute__((always_inline)) inline void busy_wait_nops(
    uint32_t iterations)
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

/*
 * Issue one untimed load. On the HIT path this reloads the cache line after
 * clflush; volatile inline assembly prevents the compiler from removing or
 * moving the access.
 */
#define LOAD_ONCE(address)                                                        \
    do {                                                                          \
        __asm__ volatile(                                                         \
            "movq (%0), %%rax\n\t"                                               \
            :                                                                     \
            : "r"(address)                                                       \
            : "rax", "memory"                                                   \
        );                                                                        \
    } while (0)

/*
 * Flush the target cache line and order completion with mfence. Settling NOPs
 * are kept at call sites so that the phases remain visible in disassembly.
 */
#define FLUSH_TARGET(address)                                                     \
    do {                                                                          \
        __asm__ volatile(                                                         \
            "clflush (%0)\n\t"                                                   \
            "mfence\n\t"                                                        \
            :                                                                     \
            : "r"(address)                                                       \
            : "memory"                                                           \
        );                                                                        \
    } while (0)

/*
 * Measure one target reload with RDTSC. Compared with the RDTSCP counterpart,
 * only the timestamp instruction changes; fences, target load, and the low
 * 32-bit subtraction are unchanged.
 *
 * Timed path:
 *     lfence
 *     rdtsc
 *     movl eax, r8d
 *     lfence
 *     movq (target), register
 *     lfence
 *     rdtsc
 *     lfence
 *     subl r8d, eax
 *
 * RDTSC does not read IA32_TSC_AUX or provide a logical CPU identifier in ECX.
 */
#define PROBE_RELOAD_RDTSC(address, elapsed)                                      \
    do {                                                                          \
        uint64_t loaded_value__;                                                  \
        __asm__ volatile(                                                         \
            "lfence\n\t"                                                        \
            "rdtsc\n\t"                                                        \
            "movl %%eax, %%r8d\n\t"                                             \
            "lfence\n\t"                                                        \
            "movq (%2), %1\n\t"                                                  \
            "lfence\n\t"                                                        \
            "rdtsc\n\t"                                                        \
            "lfence\n\t"                                                        \
            "subl %%r8d, %%eax\n\t"                                              \
            : "=&a"(elapsed), "=&r"(loaded_value__)                             \
            : "r"(address)                                                       \
            : "rdx", "r8", "cc", "memory"                                    \
        );                                                                        \
        (void)loaded_value__;                                                     \
    } while (0)

int main(void)
{
    /* Pin execution before establishing measurement state. */
    if (bind_to_cpu(TARGET_CPU) != 0) {
        return 1;
    }

    /* Query the base page size instead of assuming 4 KiB pages. */
    const long page_size = sysconf(_SC_PAGESIZE);

    if (page_size <= 0) {
        perror("sysconf");
        return 1;
    }

    /* Open the file that backs the shared target page. */
    const int fd = open(TARGET_FILE, O_RDONLY);

    if (fd < 0) {
        perror("open");
        return 1;
    }

    /* Map one shared, read-only, file-backed page for Flush+Reload. */
    uint8_t *mapping = mmap(
        NULL,
        (size_t)page_size,
        PROT_READ,
        MAP_SHARED,
        fd,
        0
    );

    close(fd);

    if (mapping == MAP_FAILED) {
        perror("mmap");
        return 1;
    }

    /* Select a fixed cache line within the mapped page. */
    uint8_t *const target = mapping + TARGET_OFFSET;

    /*
     * Touch the target before measurement to resolve the initial page fault and
     * establish page-table and TLB state.
     */
    LOAD_ONCE(target);

    /* Establish a known initial cache state. */
    FLUSH_TARGET(target);
    busy_wait_nops(FLUSH_SETTLE_NOPS);

    /*
     * Warm up the complete HIT, MISS, and inter-round paths; results are
     * discarded.
     */
    for (int i = 0; i < WARMUP_ROUNDS; ++i) {
        uint32_t unused_hit;
        uint32_t unused_miss;

        /* Warm-up HIT: flush -> settle -> load -> settle -> RDTSC probe. */
        FLUSH_TARGET(target);
        busy_wait_nops(FLUSH_SETTLE_NOPS);

        LOAD_ONCE(target);
        busy_wait_nops(RELOAD_SETTLE_NOPS);

        PROBE_RELOAD_RDTSC(target, unused_hit);

        /* Warm-up MISS: flush -> two delays -> RDTSC probe. */
        FLUSH_TARGET(target);
        busy_wait_nops(FLUSH_SETTLE_NOPS);
        busy_wait_nops(RELOAD_SETTLE_NOPS);

        PROBE_RELOAD_RDTSC(target, unused_miss);

        /* Separate warm-up rounds. */
        busy_wait_nops(ROUND_GAP_NOPS);
    }

    /* Record one HIT and one MISS in each sampling round. */
    for (int i = 0; i < SAMPLE_COUNT; ++i) {
        /*
         * HIT sample: begin from a flushed line.
         */
        FLUSH_TARGET(target);

        /* Let the flush settle without accessing the target. */
        busy_wait_nops(FLUSH_SETTLE_NOPS);

        /* Create the HIT state with an untimed preparation load. */
        LOAD_ONCE(target);

        /* Separate cache-line refill from the timed probe. */
        busy_wait_nops(RELOAD_SETTLE_NOPS);

        /* Measure the cached reload with RDTSC. */
        PROBE_RELOAD_RDTSC(target, hit_ticks[i]);

        /*
         * MISS sample: flush again to establish its own initial state.
         */
        FLUSH_TARGET(target);

        /* Apply the same post-flush delay as the HIT path. */
        busy_wait_nops(FLUSH_SETTLE_NOPS);

        /*
         * Keep the line flushed while matching the HIT path's second delay.
         */
        busy_wait_nops(RELOAD_SETTLE_NOPS);

        /* Measure the uncached reload with RDTSC. */
        PROBE_RELOAD_RDTSC(target, miss_ticks[i]);

        /* Keep the inter-round delay outside both measured intervals. */
        busy_wait_nops(ROUND_GAP_NOPS);
    }

    /*
     * Emit the minimal CSV after all samples are recorded. No extra statistics
     * or environment information are printed.
     */
    printf("index,hit_ticks,miss_ticks\n");

    for (int i = 0; i < SAMPLE_COUNT; ++i) {
        printf("%d,%u,%u\n", i, hit_ticks[i], miss_ticks[i]);
    }

    /* Release the file-backed mapping. */
    if (munmap(mapping, (size_t)page_size) != 0) {
        perror("munmap");
        return 1;
    }

    return 0;
}
