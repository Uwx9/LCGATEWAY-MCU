/*
 * 测试: IAP 基础流程（test_iap_basic）
 * 测试方法: 主机每个帧发送一次
 * 测试目标: 每一帧都得到正确响应
 * 测试目的: 最简单的 IAP 协议流程测试
 *
 * 编译：
 *   cc -std=gnu11 -Wall -Wextra -O2 test_iap_basic.c frame_io.c iap_flow.c -o test_iap_basic
 *
 * 运行：
 *   ./test_iap_basic [/dev/ttyUSB0]
 *
 * 流程：0x03 启动（带版本号） -> 0x04 文件大小 -> 0x05 擦除
 *       -> 0x06 一包数据 -> 0x07 总包数 -> 0x08 总校验
 * 0x09 会写 metadata 并让 bootloader 拷贝，本测试用的是假数据，不能执行；
 * 完整升级流程请用 test_iap_upgrade（真实 ELF）。
 * 每一步都校验应答帧的 CMD/LEN/DATA/CRC。
 */

#include <stdint.h>
#include <stdio.h>

#include "frame_io.h"
#include "iap_flow.h"

#define PAYLOAD_LEN     100U

static uint8_t g_payload[PAYLOAD_LEN];

static int report(const char *name, int err, const uint8_t *data, uint8_t data_len)
{
    if (err != ACK_OK) {
        printf("FAIL %-28s: %s\n", name, ack_str(err));
        return -1;
    }

    printf("PASS %-28s", name);
    if (data_len != 0U) {
        printf(" data=");
        dump_hex(data, data_len);
    }
    printf("\n");
    return 0;
}

int main(int argc, char **argv)
{
    const char *dev = (argc > 1) ? argv[1] : "/dev/ttyUSB0";
    uint8_t ver[2] = {0x00, 0x02};
    uint8_t can_upgrade = 0U;
    uint16_t crc;
    size_t i;
    int keepalive = 0;
    int ret = 1;

    setvbuf(stdout, NULL, _IOLBF, 0);

    for (i = 0U; i < PAYLOAD_LEN; i++) {
        g_payload[i] = (uint8_t)(i * 7U + 3U);
    }
    crc = crc16(g_payload, PAYLOAD_LEN);

    if (serial_open(dev) != 0) {
        return 1;
    }

    printf("IAP basic on %s, payload=%u, crc=0x%04X\n",
           dev, PAYLOAD_LEN, crc);

    if (report("start upgrade", iap_start(ver, &can_upgrade),
               &can_upgrade, 1U) != 0) {
        goto out;
    }
    if (can_upgrade != 1U) {
        printf("FAIL %-28s: version %u.%u is not newer\n", "start upgrade",
               (unsigned)ver[0], (unsigned)ver[1]);
        goto out;
    }
    if (report("file size", iap_size(PAYLOAD_LEN), NULL, 0U) != 0) {
        goto out;
    }

    {
        int e = iap_erase(&keepalive);

        if (e != ACK_OK) {
            printf("FAIL %-28s: %s\n", "erase", ack_str(e));
            goto out;
        }
        printf("PASS %-28s keepalive=%d\n", "erase", keepalive);
    }

    if (report("send data id=0",
               iap_packet(0U, g_payload, (uint8_t)PAYLOAD_LEN), NULL, 0U) != 0) {
        goto out;
    }
    if (report("packet count", iap_packet_count(1U), NULL, 0U) != 0) {
        goto out;
    }
    if (report("total crc", iap_crc(crc), NULL, 0U) != 0) {
        goto out;
    }
    printf("SKIP finish/reboot (fake payload; use test_iap_upgrade)\n");

    printf("IAP BASIC PASS\n");
    ret = 0;

out:
    if (ret != 0) {
        printf("IAP BASIC FAIL\n");
    }
    serial_close();
    return ret;
}
