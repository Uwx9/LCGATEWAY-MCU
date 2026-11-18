/*
 * 测试: DMA 回绕计数（test_dma_wrap）
 * 测试方法: 固件 TEST5 版提供 0x7E 测试命令，把 rx_dma_wraps 预置到 0xFFFFFF00，
 *           并按 BUF_SIZE*rx_dma_wraps 同步设置 read_total 和三个回绕计数
 * 测试目标: 预置后 dma_total_wraps=read_total_wraps=255、rx_dma_wraps_wraps=0；
 *           再传 0x10000 字节越过边界后变为 256/256/1，
 *           且每一帧都得到正确响应（DEBUG 的 avail 断言不触发）
 * 测试目的: 证明 32 位绝对字节计数和回绕计数在同步回绕时仍然自洽
 *
 * 编译:
 *   cc -std=gnu11 -Wall -Wextra -O2 test_dma_wrap.c frame_io.c -o test_dma_wrap
 *
 * 运行:
 *   ./test_dma_wrap [/dev/ttyUSB0]
 *
 * 注意: 固件需要启用 src/parse.h 里的 TEST5（默认开启）；建议用 make 的
 *       debug 构建，parse.c 里的 avail 断言才会生效。
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "frame_io.h"

#define TEST_OP_PRESET   0x00U
#define TEST_OP_STATS    0x01U

#define TEST_DATA_LEN    92U    /* LEN=97，整帧 97 字节 */
#define TEST_FRAME_SIZE  (FRAME_OVERHEAD + TEST_DATA_LEN)
#define TEST_LAP         0xFFFFFF00U
#define WRAP_MARGIN      4096U

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

/*
 * 发一条 0x7E 命令并取回 5 个计数：
 *   rx_dma_wraps, read_total, dma_total_wraps,
 *   read_total_wraps, rx_dma_wraps_wraps
 */
static int run_cmd(uint8_t op, uint32_t lap, uint32_t v[5])
{
    uint8_t d[TEST_DATA_LEN];
    uint8_t got[20];
    unsigned i;

    memset(d, 0xA5, sizeof(d));
    d[0] = op;
    put_be32(d + 1U, lap);

    if (send_cmd(TEST_CMD, d, (uint8_t)sizeof(d)) != 0) {
        printf("FAIL cmd 0x%02X: send error\n", op);
        return -1;
    }
    if (recv_ack(IAP_ACK(TEST_CMD), NULL, (uint8_t)sizeof(got), NORMAL_TIMEOUT,
                 got, NULL) != ACK_OK) {
        printf("FAIL cmd 0x%02X: ack error\n", op);
        return -1;
    }

    for (i = 0U; i < 5U; i++) {
        v[i] = be32(got + 4U * i);
    }
    return 0;
}

static int send_data(uint32_t seq)
{
    uint8_t d[TEST_DATA_LEN];
    size_t i;

    d[0] = TEST_OP_STATS;
    for (i = 1U; i < sizeof(d); i++) {
        d[i] = (uint8_t)(i + seq);
    }

    if (send_cmd(TEST_CMD, d, (uint8_t)sizeof(d)) != 0) {
        return -1;
    }
    return recv_ack(IAP_ACK(TEST_CMD), NULL, 20U, NORMAL_TIMEOUT, NULL, NULL);
}

static void print_stats(const char *tag, const uint32_t v[5])
{
    printf("%s dma_wraps=0x%08X read_total=0x%08X dma_total_wraps=%u "
           "read_total_wraps=%u wrap_wraps=%u\n",
           tag, (unsigned)v[0], (unsigned)v[1], (unsigned)v[2],
           (unsigned)v[3], (unsigned)v[4]);
}

int main(int argc, char **argv)
{
    const char *dev = (argc > 1) ? argv[1] : "/dev/ttyUSB0";
    uint32_t before[5];
    uint32_t after[5];
    uint32_t expected_wraps = TEST_LAP >> 24;
    uint32_t to_wrap;
    uint32_t frames;
    uint32_t i;
    uint64_t total_bytes;
    int ret = 1;

    setvbuf(stdout, NULL, _IOLBF, 0);

    if (serial_open(dev) != 0) {
        return 1;
    }

    printf("DMA wrap test on %s, lap=0x%08X\n", dev, (unsigned)TEST_LAP);

    if (run_cmd(TEST_OP_PRESET, TEST_LAP, before) != 0) {
        goto out;
    }
    print_stats("preset:", before);

    total_bytes = (uint64_t)TEST_LAP * 256U + (before[1] & 0xFFU);
    printf("total=%llu bytes (~%llu GiB)\n",
           (unsigned long long)total_bytes,
           (unsigned long long)(total_bytes >> 30));

    if (before[0] != TEST_LAP) {
        printf("FAIL rx_dma_wraps=0x%08X (want 0x%08X)\n",
               (unsigned)before[0], (unsigned)TEST_LAP);
        goto out;
    }
    if ((before[2] != expected_wraps) || (before[3] != expected_wraps)) {
        printf("FAIL wrap counts=%u/%u (want %u/%u)\n",
               (unsigned)before[2], (unsigned)before[3],
               (unsigned)expected_wraps, (unsigned)expected_wraps);
        goto out;
    }
    if (before[4] != 0U) {
        printf("FAIL rx_dma_wraps_wraps=%u (want 0)\n", (unsigned)before[4]);
        goto out;
    }

    /* 再传这么多字节，dma_total/read_total 和 rx_dma_wraps 一起回绕 */
    to_wrap = 0U - before[1];
    if ((to_wrap > 0x10000U) || (to_wrap < (0x10000U - 255U))) {
        printf("FAIL to_wrap=%u out of range (preset not applied?)\n",
               (unsigned)to_wrap);
        goto out;
    }
    frames  = (to_wrap + WRAP_MARGIN) / TEST_FRAME_SIZE + 2U;

    printf("to_wrap=%u, frames=%u\n", (unsigned)to_wrap, (unsigned)frames);

    for (i = 0U; i < frames; i++) {
        int e = send_data(i);

        if (e != ACK_OK) {
            printf("FAIL frame %u: %s\n", (unsigned)i, ack_str(e));
            goto out;
        }
        if (((i + 1U) % 100U) == 0U) {
            printf("  %u/%u\n", (unsigned)(i + 1U), (unsigned)frames);
        }
    }

    if (run_cmd(TEST_OP_STATS, 0U, after) != 0) {
        goto out;
    }
    print_stats("after :", after);

    if (after[0] >= 0x1000U) {
        printf("FAIL rx_dma_wraps=0x%08X did not wrap\n", (unsigned)after[0]);
        goto out;
    }
    if ((after[2] != expected_wraps + 1U) ||
        (after[3] != expected_wraps + 1U)) {
        printf("FAIL wrap counts=%u/%u (want %u/%u)\n",
               (unsigned)after[2], (unsigned)after[3],
               (unsigned)(expected_wraps + 1U), (unsigned)(expected_wraps + 1U));
        goto out;
    }
    if (after[4] != 1U) {
        printf("FAIL rx_dma_wraps_wraps=%u (want 1)\n", (unsigned)after[4]);
        goto out;
    }

    printf("DMA WRAP PASS\n");
    ret = 0;

out:
    if (ret != 0) {
        printf("DMA WRAP FAIL\n");
    }
    serial_close();
    return ret;
}
