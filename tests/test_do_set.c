/*
 * 测试: DO 设置（test_do_set）
 * 测试方法: 通过 0x01 SET_DO 逐路设置 DO1~DO16 为高、再逐路设置回低，
 *           最后测试非法 DO 编号和非法状态。
 * 测试目标: 每条设置命令都收到 0x81 应答；正常设置 result=0，
 *           非法参数 result!=0。
 * 测试目的: 验证 DO 指令解析、DO 编号映射和 GPIO 配置路径能正常执行。
 *
 * 注意: 当前协议没有回读 DO 引脚状态的指令，本测试只能验证 MCU 返回的结果；
 *       实际输出电平需要用 LED、万用表或示波器在对应引脚上确认。
 *
 * 编译:
 *   cc -std=gnu11 -Wall -Wextra -O2 test_do_set.c frame_io.c iap_flow.c -o test_do_set
 *
 * 运行（在 tests 目录下）:
 *   ./test_do_set [/dev/ttyUSB0]
 */

#include <stdint.h>
#include <stdio.h>

#include "frame_io.h"
#include "iap_flow.h"

#define DO_COUNT  16U

static int check_do(uint8_t id, uint8_t status, int expect_ok)
{
    char name[32];
    uint8_t result = 0xFFU;
    int e;

    snprintf(name, sizeof(name), "set DO%u=%u", (unsigned)id, (unsigned)status);
    e = iap_set_do(id, status, &result);
    if (e != ACK_OK) {
        printf("FAIL %-28s: %s\n", name, ack_str(e));
        return -1;
    }

    if ((expect_ok != 0) && (result != 0U)) {
        printf("FAIL %-28s: result=%u\n", name, (unsigned)result);
        return -1;
    }
    if ((expect_ok == 0) && (result == 0U)) {
        printf("FAIL %-28s: invalid argument accepted\n", name);
        return -1;
    }

    printf("PASS %-28s result=%u\n", name, (unsigned)result);
    return 0;
}

int main(int argc, char **argv)
{
    const char *dev = (argc > 1) ? argv[1] : "/dev/ttyUSB0";
    uint8_t id;
    int ret = 1;

    setvbuf(stdout, NULL, _IOLBF, 0);

    if (serial_open(dev) != 0) {
        return 1;
    }

    printf("DO test on %s, channels=%u\n", dev, (unsigned)DO_COUNT);

    /* 全部拉高 */
    for (id = 1U; id <= DO_COUNT; id++) {
        if (check_do(id, 1U, 1) != 0) {
            goto out;
        }
    }

    /* 全部拉低，结束时停在安全状态 */
    for (id = 1U; id <= DO_COUNT; id++) {
        if (check_do(id, 0U, 1) != 0) {
            goto out;
        }
    }

    /* 非法参数 */
    if (check_do(0U, 1U, 0) != 0) {
        goto out;
    }
    if (check_do(DO_COUNT + 1U, 0U, 0) != 0) {
        goto out;
    }
    if (check_do(1U, 2U, 0) != 0) {
        goto out;
    }

    printf("DO SET PASS\n");
    ret = 0;

out:
    if (ret != 0) {
        printf("DO SET FAIL\n");
    }
    serial_close();
    return ret;
}
