#ifndef AES_FR_AES_H
#define AES_FR_AES_H

#include <stdint.h>

typedef struct {
    uint32_t *tables;       /* 4 x 256 个 uint32_t，来自 mmap 的共享只读页 */
    uint32_t round_key[44]; /* 只存在 victim 进程地址空间 */
} aes_context_t;

void aes_init_tables(uint32_t *tables);
void aes_expand_key(aes_context_t *ctx, const uint8_t key[16]);
void aes_encrypt_full(const aes_context_t *ctx,
                      const uint8_t in[16], uint8_t out[16]);

/* victim 使用：执行 AddRoundKey + 第一轮 T-table，返回第一轮状态。 */
void aes_first_round(const aes_context_t *ctx,
                     const uint8_t in[16],
                     uint32_t state_out[4]);

/* victim 使用：从第一轮状态继续完成第 2～10 轮。 */
void aes_finish_after_first_round(const aes_context_t *ctx,
                                  const uint32_t first_state[4],
                                  uint8_t out[16]);

#endif
