#ifndef AES_FR_TIMING_H
#define AES_FR_TIMING_H

#include <stdint.h>

/* 用户之前 Week 1/Week 2 使用的 NOP 忙等路径。 */
static inline void busy_wait_nops(uint32_t iterations)
{
    while (iterations != 0U) {
        __asm__ volatile("nop\n\t" ::: "memory");
        --iterations;
    }
}

/* 不进入 cache timing 区间的 TSC 采样，用于记录 victim 粗略运行时间。 */
static inline uint64_t read_tsc_rdtscp(void)
{
    uint32_t lo;
    uint32_t hi;

    __asm__ volatile(
        "lfence\n\t"
        "rdtscp\n\t"
        "movl %%eax, %0\n\t"
        "movl %%edx, %1\n\t"
        "lfence\n\t"
        : "=r"(lo), "=r"(hi)
        :
        : "rax", "rdx", "rcx", "memory");

    return ((uint64_t)hi << 32) | lo;
}

/*
 * Week 1/Week 2 同款的极简 timed reload 路径。
 * 计时区间内保留：lfence -> rdtscp -> mov -> lfence -> movq -> lfence
 * -> rdtscp -> lfence -> sub。
 * 不在这里执行 flush、printf、函数调用或阈值判断。
 */
#define PROBE_RELOAD_ONLY(address, elapsed)                                      \
    do {                                                                          \
        uint64_t loaded_value__;                                                 \
        __asm__ volatile(                                                         \
            "lfence\n\t"                                                        \
            "rdtscp\n\t"                                                        \
            "movl %%eax, %%r8d\n\t"                                             \
            "lfence\n\t"                                                        \
            "movq (%2), %1\n\t"                                                 \
            "lfence\n\t"                                                        \
            "rdtscp\n\t"                                                        \
            "lfence\n\t"                                                        \
            "subl %%r8d, %%eax\n\t"                                             \
            : "=&a"(elapsed), "=&r"(loaded_value__)                             \
            : "r"(address)                                                       \
            : "rdx", "rcx", "r8", "cc", "memory"                            \
        );                                                                        \
        (void)loaded_value__;                                                      \
    } while (0)

#define CLFLUSH_LINE(address)                                                     \
    do {                                                                          \
        __asm__ volatile("clflush (%0)\n\t"                                      \
                         :                                                       \
                         : "r"(address)                                          \
                         : "memory");                                            \
    } while (0)

#define MFENCE_ALL()                                                              \
    do {                                                                          \
        __asm__ volatile("mfence\n\t" ::: "memory");                           \
    } while (0)

#endif
