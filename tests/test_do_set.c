/*
 * 测试: DO 设置压力测试（test_do_set）
 *
 * 覆盖:
 *   - DO1~DO16 全部通道，status=0/1
 *   - 全高、全低
 *   - walking one：逐路开/关，保证每个通道都单独动作
 *   - 0x5555 / 0xAAAA 交替模式
 *   - 随机 16 位模式（固定种子，失败可复现）
 *   - 边界通道 DO1、DO16 重复设置
 *   - 非法 DO 编号 0/17/255、非法状态 2/255
 *   - 最后 DO1~DO16 全部置高
 *
 * 每轮默认 164 帧，默认 100 轮；可通过第二个参数加大：
 *   ./test_do_set /dev/ttyUSB0 500
 *
 * 注意: 当前协议没有回读 DO 引脚状态的指令，本测试只能验证
 *       0x81 应答和 result 是否正确；实际电平需要用 LED/万用表/示波器确认。
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "frame_io.h"
#include "iap_flow.h"

#define DO_COUNT          16U
#define DEFAULT_CYCLES    100UL
#define RANDOM_PER_CYCLE  4U
#define PROGRESS_EVERY    10UL

static unsigned long g_cycle;
static unsigned long g_frames;
static unsigned long g_failures;
static const char *g_phase = "";

static uint32_t s_rng = 0x12345678U;

static uint32_t rng_next(void)
{
    uint32_t x = s_rng;

    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s_rng = x;
    return x;
}

static int do_cmd(uint8_t id, uint8_t status, int expect_ok)
{
    uint8_t result = 0xFFU;
    int e;

    g_frames++;
    e = iap_set_do(id, status, &result);
    if (e != ACK_OK) {
        g_failures++;
        printf("FAIL cycle=%lu phase=%s id=%u status=%u ack=%s\n",
               g_cycle, g_phase, (unsigned)id, (unsigned)status, ack_str(e));
        return -1;
    }

    if ((expect_ok != 0) && (result != 0U)) {
        g_failures++;
        printf("FAIL cycle=%lu phase=%s id=%u status=%u result=%u\n",
               g_cycle, g_phase, (unsigned)id, (unsigned)status,
               (unsigned)result);
        return -1;
    }
    if ((expect_ok == 0) && (result == 0U)) {
        g_failures++;
        printf("FAIL cycle=%lu phase=%s id=%u status=%u: invalid accepted\n",
               g_cycle, g_phase, (unsigned)id, (unsigned)status);
        return -1;
    }

    return 0;
}

int main(int argc, char **argv)
{
    const char *dev = (argc > 1) ? argv[1] : "/dev/ttyUSB0";
    unsigned long cycles = DEFAULT_CYCLES;
    unsigned long cycle;
    unsigned long r;
    uint8_t id;
    struct timespec t0;
    struct timespec t1;
    double elapsed;
    int ret = 1;

    setvbuf(stdout, NULL, _IOLBF, 0);

    if (argc > 2) {
        char *end = NULL;
        unsigned long v = strtoul(argv[2], &end, 0);

        if ((end == argv[2]) || (*end != '\0') || (v == 0UL)) {
            fprintf(stderr, "usage: %s [device] [cycles]\n", argv[0]);
            return 2;
        }
        cycles = v;
    }

    if (serial_open(dev) != 0) {
        return 1;
    }

    printf("DO stress test on %s, channels=%u, cycles=%lu\n",
           dev, (unsigned)DO_COUNT, cycles);
    printf("patterns: all-high/low, walking-one, 0x5555/0xAAAA, "
           "random x%u, repeated, invalid id/status\n",
           (unsigned)RANDOM_PER_CYCLE);

    clock_gettime(CLOCK_MONOTONIC, &t0);

    for (cycle = 0UL; cycle < cycles; cycle++) {
        g_cycle = cycle;

        g_phase = "all-high";
        for (id = 1U; id <= DO_COUNT; id++) {
            if (do_cmd(id, 1U, 1) != 0) {
                goto out;
            }
        }

        g_phase = "all-low";
        for (id = 1U; id <= DO_COUNT; id++) {
            if (do_cmd(id, 0U, 1) != 0) {
                goto out;
            }
        }

        g_phase = "walking-one";
        for (id = 1U; id <= DO_COUNT; id++) {
            if (do_cmd(id, 1U, 1) != 0) {
                goto out;
            }
            if (do_cmd(id, 0U, 1) != 0) {
                goto out;
            }
        }

        g_phase = "alternating-5555";
        for (id = 1U; id <= DO_COUNT; id++) {
            if (do_cmd(id, (uint8_t)((0x5555U >> (id - 1U)) & 1U), 1) != 0) {
                goto out;
            }
        }

        g_phase = "alternating-aaaa";
        for (id = 1U; id <= DO_COUNT; id++) {
            if (do_cmd(id, (uint8_t)((0xAAAAU >> (id - 1U)) & 1U), 1) != 0) {
                goto out;
            }
        }

        g_phase = "random";
        for (r = 0UL; r < RANDOM_PER_CYCLE; r++) {
            uint16_t mask = (uint16_t)(rng_next() & 0xFFFFU);

            for (id = 1U; id <= DO_COUNT; id++) {
                if (do_cmd(id, (uint8_t)((mask >> (id - 1U)) & 1U), 1) != 0) {
                    goto out;
                }
            }
        }

        g_phase = "repeat-boundary";
        if (do_cmd(1U, 1U, 1) != 0) {
            goto out;
        }
        if (do_cmd(1U, 1U, 1) != 0) {
            goto out;
        }
        if (do_cmd(DO_COUNT, 0U, 1) != 0) {
            goto out;
        }
        if (do_cmd(DO_COUNT, 0U, 1) != 0) {
            goto out;
        }

        if ((((cycle + 1UL) % PROGRESS_EVERY) == 0UL) ||
            ((cycle + 1UL) == cycles)) {
            printf("cycle %lu/%lu, frames=%lu, failures=%lu\n",
                   cycle + 1UL, cycles, g_frames, g_failures);
        }
    }

    g_cycle = cycles;

    g_phase = "invalid-id";
    if (do_cmd(0U, 1U, 0) != 0) {
        goto out;
    }
    if (do_cmd(DO_COUNT + 1U, 0U, 0) != 0) {
        goto out;
    }
    if (do_cmd(255U, 1U, 0) != 0) {
        goto out;
    }

    g_phase = "invalid-status";
    if (do_cmd(1U, 2U, 0) != 0) {
        goto out;
    }
    if (do_cmd(DO_COUNT, 255U, 0) != 0) {
        goto out;
    }

    g_phase = "final-all-high";
    for (id = 1U; id <= DO_COUNT; id++) {
        if (do_cmd(id, 1U, 1) != 0) {
            goto out;
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &t1);
    elapsed = (double)(t1.tv_sec - t0.tv_sec) +
              (double)(t1.tv_nsec - t0.tv_nsec) / 1000000000.0;

    printf("DO stress: cycles=%lu frames=%lu failures=%lu time=%.2fs\n",
           cycles, g_frames, g_failures, elapsed);
    printf("coverage: DO1..DO16 x {0,1}, walking-one, 5555/AAAA, random, "
           "invalid id/status, final all-high\n");
    printf("DO SET PASS\n");
    ret = 0;

out:
    if (ret != 0) {
        printf("DO SET FAIL\n");
    }
    serial_close();
    return ret;
}
