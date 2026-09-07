#define _GNU_SOURCE

#include "aes.h"
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
#include <sys/types.h>
#include <unistd.h>

static const uint8_t victim_key[16] = {
    0x2b,0x7e,0x15,0x16,0x28,0xae,0xd2,0xa6,
    0xab,0xf7,0x15,0x88,0x09,0xcf,0x4f,0x3c
};

static int bind_process_to_cpu(int cpu)
{
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
    if (sched_setaffinity(0, sizeof(set), &set) != 0) {
        fprintf(stderr, "victim: sched_setaffinity(cpu=%d): %s\n", cpu, strerror(errno));
        return -1;
    }
    return 0;
}

static void usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s --table PATH --ctrl NAME --traces N --cpu CPU\n", prog);
}

static int wait_phase(shared_control_t *ctrl, int wanted)
{
    for (;;) {
        const int fatal = atomic_load_explicit(&ctrl->fatal_error, memory_order_acquire);
        const int phase = atomic_load_explicit(&ctrl->phase, memory_order_acquire);
        if (fatal != 0 || phase == PHASE_ERROR) {
            return -1;
        }
        if (phase == wanted) {
            return 0;
        }
        __asm__ volatile("nop\n\t" ::: "memory");
    }
}

static int create_and_map_table(const char *path, uint32_t **out)
{
    int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        fprintf(stderr, "victim: open table %s: %s\n", path, strerror(errno));
        return -1;
    }
    if (ftruncate(fd, TABLE_FILE_BYTES) != 0) {
        fprintf(stderr, "victim: ftruncate table: %s\n", strerror(errno));
        close(fd);
        return -1;
    }

    void *map = mmap(NULL, TABLE_FILE_BYTES, PROT_READ | PROT_WRITE,
                     MAP_SHARED, fd, 0);
    close(fd);
    if (map == MAP_FAILED) {
        fprintf(stderr, "victim: mmap table: %s\n", strerror(errno));
        return -1;
    }

    aes_init_tables((uint32_t *)map);
    if (msync(map, TABLE_FILE_BYTES, MS_SYNC) != 0) {
        fprintf(stderr, "victim: msync table: %s\n", strerror(errno));
        munmap(map, TABLE_FILE_BYTES);
        return -1;
    }

    /* 初始化后只读，避免 victim 后续意外修改共享 T-table。 */
    if (mprotect(map, TABLE_FILE_BYTES, PROT_READ) != 0) {
        fprintf(stderr, "victim: mprotect table read-only: %s\n", strerror(errno));
        munmap(map, TABLE_FILE_BYTES);
        return -1;
    }

    *out = (uint32_t *)map;
    return 0;
}

int main(int argc, char **argv)
{
    const char *table_path = DEFAULT_TABLE_FILE;
    const char *ctrl_name = DEFAULT_CTRL_NAME;
    int trace_count = DEFAULT_TRACE_COUNT;
    int cpu = DEFAULT_VICTIM_CPU;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--table") == 0 && i + 1 < argc) table_path = argv[++i];
        else if (strcmp(argv[i], "--ctrl") == 0 && i + 1 < argc) ctrl_name = argv[++i];
        else if (strcmp(argv[i], "--traces") == 0 && i + 1 < argc) trace_count = atoi(argv[++i]);
        else if (strcmp(argv[i], "--cpu") == 0 && i + 1 < argc) cpu = atoi(argv[++i]);
        else { usage(argv[0]); return 2; }
    }

    if (trace_count <= 0 || trace_count > 1000000 || cpu < 0) {
        fprintf(stderr, "victim: invalid traces/cpu\n");
        return 2;
    }

    (void)shm_unlink(ctrl_name); /* 仅清理本实验使用的固定控制对象。 */
    int shm_fd = shm_open(ctrl_name, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (shm_fd < 0) {
        fprintf(stderr, "victim: shm_open %s: %s\n", ctrl_name, strerror(errno));
        return 1;
    }
    if (ftruncate(shm_fd, (off_t)sizeof(shared_control_t)) != 0) {
        fprintf(stderr, "victim: ftruncate control: %s\n", strerror(errno));
        close(shm_fd);
        shm_unlink(ctrl_name);
        return 1;
    }

    shared_control_t *ctrl = mmap(NULL, sizeof(*ctrl), PROT_READ | PROT_WRITE,
                                   MAP_SHARED, shm_fd, 0);
    close(shm_fd);
    if (ctrl == MAP_FAILED) {
        fprintf(stderr, "victim: mmap control: %s\n", strerror(errno));
        shm_unlink(ctrl_name);
        return 1;
    }
    memset(ctrl, 0, sizeof(*ctrl));
    ctrl->magic = CTRL_MAGIC;
    ctrl->version = 1;
    ctrl->trace_count = (uint32_t)trace_count;
    ctrl->victim_cpu = (uint32_t)cpu;
    atomic_store_explicit(&ctrl->phase, PHASE_READY, memory_order_relaxed);

    if (bind_process_to_cpu(cpu) != 0) {
        atomic_store_explicit(&ctrl->fatal_error, 1, memory_order_release);
        atomic_store_explicit(&ctrl->phase, PHASE_ERROR, memory_order_release);
        munmap(ctrl, sizeof(*ctrl));
        shm_unlink(ctrl_name);
        return 1;
    }

    uint32_t *tables = NULL;
    if (create_and_map_table(table_path, &tables) != 0) {
        atomic_store_explicit(&ctrl->fatal_error, 1, memory_order_release);
        atomic_store_explicit(&ctrl->phase, PHASE_ERROR, memory_order_release);
        munmap(ctrl, sizeof(*ctrl));
        shm_unlink(ctrl_name);
        return 1;
    }

    aes_context_t aes = { .tables = tables };
    aes_expand_key(&aes, victim_key);

    uint8_t test_pt[16] = {
        0x32,0x43,0xf6,0xa8,0x88,0x5a,0x30,0x8d,
        0x31,0x31,0x98,0xa2,0xe0,0x37,0x07,0x34
    };
    uint8_t expected_ct[16] = {
        0x39,0x25,0x84,0x1d,0x02,0xdc,0x09,0xfb,
        0xdc,0x11,0x85,0x97,0x19,0x6a,0x0b,0x32
    };
    uint8_t test_ct[16];
    aes_encrypt_full(&aes, test_pt, test_ct);
    if (memcmp(test_ct, expected_ct, 16) != 0) {
        fprintf(stderr, "victim: AES self-test failed\n");
        atomic_store_explicit(&ctrl->fatal_error, 1, memory_order_release);
        atomic_store_explicit(&ctrl->phase, PHASE_ERROR, memory_order_release);
        munmap(tables, TABLE_FILE_BYTES);
        munmap(ctrl, sizeof(*ctrl));
        shm_unlink(ctrl_name);
        return 1;
    }

    atomic_store_explicit(&ctrl->table_ready, 1, memory_order_release);
    printf("victim ready: cpu=%d, traces=%d, table=%s\n", cpu, trace_count, table_path);
    printf("victim AES self-test passed; master key is private to victim code path.\n");
    fflush(stdout);

    for (int trace_id = 0; trace_id < trace_count; ++trace_id) {
        if (wait_phase(ctrl, PHASE_START) != 0) break;

        uint8_t plaintext[16];
        memcpy(plaintext, ctrl->current_plaintext, sizeof(plaintext));
        ctrl->current_trace = (uint32_t)trace_id;

        uint32_t first_state[4];
        ctrl->victim_round_start_tsc = read_tsc_rdtscp();
        aes_first_round(&aes, plaintext, first_state);
        ctrl->victim_round_done_tsc = read_tsc_rdtscp();

        atomic_store_explicit(&ctrl->phase, PHASE_ROUND_DONE, memory_order_release);

        if (wait_phase(ctrl, PHASE_MEASURE_DONE) != 0) break;

        ctrl->victim_rest_start_tsc = read_tsc_rdtscp();
        aes_finish_after_first_round(&aes, first_state, ctrl->current_ciphertext);
        ctrl->victim_trace_done_tsc = read_tsc_rdtscp();

        atomic_store_explicit(&ctrl->phase,
                              (trace_id + 1 == trace_count) ? PHASE_DONE : PHASE_READY,
                              memory_order_release);
    }

    const int phase = atomic_load_explicit(&ctrl->phase, memory_order_acquire);
    if (phase != PHASE_DONE) {
        atomic_store_explicit(&ctrl->fatal_error, 1, memory_order_release);
        atomic_store_explicit(&ctrl->phase, PHASE_ERROR, memory_order_release);
    }

    munmap(tables, TABLE_FILE_BYTES);
    munmap(ctrl, sizeof(*ctrl));
    shm_unlink(ctrl_name);
    return (phase == PHASE_DONE) ? 0 : 1;
}
