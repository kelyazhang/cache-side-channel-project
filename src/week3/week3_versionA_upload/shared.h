#ifndef AES_FR_SHARED_H
#define AES_FR_SHARED_H

#define _GNU_SOURCE

#include <stdatomic.h>
#include <stdint.h>

#define AES_BLOCK_BYTES 16
#define TABLE_COUNT 4
#define TABLE_ENTRIES 256
#define TABLE_ENTRY_BYTES 4
#define CACHE_LINE_BYTES 64
#define ENTRIES_PER_CACHE_LINE (CACHE_LINE_BYTES / TABLE_ENTRY_BYTES)
#define LINES_PER_TABLE (TABLE_ENTRIES / ENTRIES_PER_CACHE_LINE)
#define TABLE_FILE_BYTES (TABLE_COUNT * TABLE_ENTRIES * TABLE_ENTRY_BYTES)
#define TOTAL_MONITORED_LINES (TABLE_COUNT * LINES_PER_TABLE)

#define DEFAULT_CTRL_NAME "/aes_fr_versionA_ctrl"
#define DEFAULT_TABLE_FILE "ttable_shared.bin"
#define DEFAULT_RAW_CSV "trace_timings.csv"
#define DEFAULT_SYNC_CSV "victim_timing.csv"
#define DEFAULT_TRACE_COUNT 10000
#define DEFAULT_ATTACKER_CPU 3
#define DEFAULT_VICTIM_CPU 4

#define CTRL_MAGIC 0x4145534652564131ULL /* "AESFRVA1" */

/*
 * Version A 的公开同步状态。
 * READY：victim 已完成上一条 trace，或尚未开始第一条 trace。
 * START：attacker 已完成本轮 flush，victim 可以读取明文并执行第一轮。
 * ROUND_DONE：victim 已完成第一轮 T-table，attacker 可以 reload。
 * MEASURE_DONE：attacker 已保存本轮原始 ticks，victim 可以完成剩余 AES。
 * DONE：victim 已完成全部 trace。
 * ERROR：某一方检测到错误，另一方停止等待。
 */
enum {
    PHASE_READY = 0,
    PHASE_START = 1,
    PHASE_ROUND_DONE = 2,
    PHASE_MEASURE_DONE = 3,
    PHASE_DONE = 4,
    PHASE_ERROR = -1
};

/*
 * 这个结构位于 POSIX shared memory 中，只放公开控制信息、明文、密文和时间戳。
 * 不放置 AES master key，也不放置 round key。
 */
typedef struct {
    uint64_t magic;
    uint32_t version;
    uint32_t trace_count;
    uint32_t attacker_cpu;
    uint32_t victim_cpu;

    _Atomic int table_ready;
    _Atomic int phase;
    _Atomic int fatal_error;

    uint32_t current_trace;
    uint8_t current_plaintext[AES_BLOCK_BYTES];
    uint8_t current_ciphertext[AES_BLOCK_BYTES];

    /* victim 侧写入，attacker 只在本条 trace 结束后读取并保存。 */
    uint64_t victim_round_start_tsc;
    uint64_t victim_round_done_tsc;
    uint64_t victim_rest_start_tsc;
    uint64_t victim_trace_done_tsc;
} shared_control_t;

#endif
