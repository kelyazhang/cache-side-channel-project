#define _GNU_SOURCE

/*
 * Flush+Reload calibration experiment for a single process and thread.
 *
 * This revision adds explicit NOP delays after clflush and after the HIT
 * preparation load, while keeping the timed probe unchanged. The delays are
 * outside the measured interval and help separate cache-line refill from the
 * subsequent reload measurement. MISS samples use the same delay budget so
 * that both paths have comparable pre-probe timing.
 *
 * HIT sequence:
 *     clflush(target), mfence, settle, load target, settle, timed reload
 * MISS sequence:
 *     clflush(target), mfence, settle, settle, timed reload
 *
 * The probe records the difference between the low 32 bits of two RDTSCP
 * readings, reported as invariant TSC ticks. It contains no flush, delay,
 * I/O, function call, conditional branch, or 64-bit TSC reconstruction.
 *
 * Build:
 *     gcc -O2 -std=gnu11 -Wall -Wextra -march=native \
 *         -fno-pie -no-pie \
 *         flush_reload.c -o week1_probe
 *
 * Run and save CSV output:
 *     sudo taskset -c 5 ./week1_probe
 *     sudo taskset -c 5 ./week1_probe > calibration.csv
 *
 * Inspect the generated timing sequence:
 *     objdump -d -Mintel --no-show-raw-insn week1_probe > disasm.txt
 *     grep -n -C 16 "rdtscp" disasm.txt
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
#define SAMPLE_COUNT 50000

/*
 * Warm-up rounds executed before recording samples. Warm-up establishes the
 * mapping, page-table and TLB state, and exercises the flush and timing paths
 * so that first-use overhead does not enter the recorded data.
 */
#define WARMUP_ROUNDS 1000

/*
 * NOP iterations after clflush + mfence. This separates the flush from the
 * HIT preparation load or the MISS probe and can be swept independently.
 */
#define FLUSH_SETTLE_NOPS 5000U

/*
 * NOP iterations between the HIT preparation load and the timed reload.
 * The MISS path executes the same delay without loading the target, keeping
 * the pre-probe timing of both paths approximately aligned.
 */
#define RELOAD_SETTLE_NOPS 5000U

/*
 * NOP iterations between rounds. This limits carry-over from the preceding
 * miss, cache-line fill, fences, and clflush operations.
 */
#define ROUND_GAP_NOPS 20000U

/* Shared read-only file that backs the target mapping. */
#define TARGET_FILE "/bin/ls"

/* Cache-line offset within the mapping; zero is naturally page-aligned. */
#define TARGET_OFFSET 0

/*
 * Samples are buffered in memory and emitted only after measurement, keeping
 * all formatted and file I/O outside the sampling loop.
 */
static uint32_t hit_ticks[SAMPLE_COUNT];
static uint32_t miss_ticks[SAMPLE_COUNT];

/*
 * Pin the process to one logical CPU to avoid migration across cores and the
 * associated changes in private cache, TLB, and microarchitectural state.
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
 * Busy-wait for the requested number of iterations. Each iteration contains
 * a NOP plus the loop-control instructions. The wait remains in user space,
 * does not voluntarily yield the CPU, and is always outside the RDTSCP timing
 * interval. Inlining removes call overhead, while the memory clobber prevents
 * the compiler from moving target accesses across the wait.
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
 * Issue one untimed load from the target. On the HIT path, this reloads the
 * cache line after clflush; it is preparation and not part of the probe.
 */
#define LOAD_ONCE(address)                                                        \
    do {                                                                          \
        __asm__ volatile(                                                         \
            "movq (%0), %%rax\n\t"                                                \
            :                                                                     \
            : "r"(address)                                                        \
            : "rax", "memory"                                                     \
        );                                                                        \
    } while (0)

/*
 * Flush the cache line containing the target and order completion with
 * mfence. Delays remain explicit at call sites so that flush, settling, and
 * load/probe phases are distinguishable in both source and disassembly.
 */
#define FLUSH_TARGET(address)                                                     \
    do {                                                                          \
        __asm__ volatile(                                                         \
            "clflush (%0)\n\t"                                                    \
            "mfence\n\t"                                                          \
            :                                                                     \
            : "r"(address)                                                        \
            : "memory"                                                            \
        );                                                                        \
    } while (0)

/*
 * Measure one target reload. The timed path is intentionally minimal:
 *
 *     lfence
 *     rdtscp
 *     mov start EAX -> R8D
 *     lfence
 *     movq (target), destination
 *     lfence
 *     rdtscp
 *     lfence
 *     sub start from end
 *
 * elapsed receives the low-32-bit difference between the two RDTSCP values,
 * in invariant TSC ticks. The macro performs no flush, delay, I/O, function
 * call, conditional branch, or 64-bit timestamp reconstruction.
 */
#define PROBE_RELOAD_ONLY(address, elapsed)                                       \
    do {                                                                          \
        uint64_t loaded_value__;                                                   \
        __asm__ volatile(                                                         \
            "lfence\n\t"                                                          \
            "rdtscp\n\t"                                                          \
            "movl %%eax, %%r8d\n\t"                                               \
            "lfence\n\t"                                                          \
            "movq (%2), %1\n\t"                                                   \
            "lfence\n\t"                                                          \
            "rdtscp\n\t"                                                          \
            "lfence\n\t"                                                          \
            "subl %%r8d, %%eax\n\t"                                               \
            : "=&a"(elapsed), "=&r"(loaded_value__)                               \
            : "r"(address)                                                        \
            : "rdx", "rcx", "r8", "cc", "memory"                                 \
        );                                                                        \
        (void)loaded_value__;                                                      \
    } while (0)

int main(void)
{
    /* Pin execution before establishing any measurement state. */
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

    /* Map one shared, read-only, file-backed page. */
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
     * Touch the target before measurement to resolve the initial page fault
     * and establish page-table and TLB state.
     */
    LOAD_ONCE(target);

    /* Establish a known initial cache state. */
    FLUSH_TARGET(target);
    busy_wait_nops(FLUSH_SETTLE_NOPS);

    /*
     * Warm up the exact HIT and MISS paths used for recorded samples. Results
     * are discarded, but all flush, settle, and inter-round delays are kept.
     */
    for (int i = 0; i < WARMUP_ROUNDS; ++i) {
        uint32_t unused_hit;
        uint32_t unused_miss;

        /*
         * Warm-up HIT: flush, settle, reload the line, settle, then probe.
         */
        FLUSH_TARGET(target);
        busy_wait_nops(FLUSH_SETTLE_NOPS);

        LOAD_ONCE(target);
        busy_wait_nops(RELOAD_SETTLE_NOPS);

        PROBE_RELOAD_ONLY(target, unused_hit);

        /*
         * Warm-up MISS: flush, apply both delays without touching the target,
         * then probe. This approximately matches the HIT path's time window.
         */
        FLUSH_TARGET(target);
        busy_wait_nops(FLUSH_SETTLE_NOPS);
        busy_wait_nops(RELOAD_SETTLE_NOPS);

        PROBE_RELOAD_ONLY(target, unused_miss);

        /* Separate this warm-up round from the next. */
        busy_wait_nops(ROUND_GAP_NOPS);
    }

    /* Record one HIT and one MISS in each sampling round. */
    for (int i = 0; i < SAMPLE_COUNT; ++i) {
        /*
         * HIT sample: begin from a flushed line so that the prepared cache hit
         * does not inherit state from the preceding sample.
         */
        FLUSH_TARGET(target);

        /* Let the flush settle without accessing the target. */
        busy_wait_nops(FLUSH_SETTLE_NOPS);

        /* Create the HIT state with an untimed preparation load. */
        LOAD_ONCE(target);

        /* Separate cache-line refill from the timed probe. */
        busy_wait_nops(RELOAD_SETTLE_NOPS);

        /* Measure the cached reload; the probe does not flush afterward. */
        PROBE_RELOAD_ONLY(target, hit_ticks[i]);

        /*
         * MISS sample: this flush starts the MISS path; it is not a trailing
         * operation of the preceding HIT probe.
         */
        FLUSH_TARGET(target);

        /* Apply the same post-flush delay as the HIT path. */
        busy_wait_nops(FLUSH_SETTLE_NOPS);

        /*
         * Match the HIT path's second delay without loading the target, so the
         * line remains flushed while pre-probe timing stays comparable.
         */
        busy_wait_nops(RELOAD_SETTLE_NOPS);

        /* Measure the uncached reload. */
        PROBE_RELOAD_ONLY(target, miss_ticks[i]);

        /* Keep the inter-round delay outside both measured intervals. */
        busy_wait_nops(ROUND_GAP_NOPS);
    }

    /* Emit all samples only after the measurement loop has completed. */
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
