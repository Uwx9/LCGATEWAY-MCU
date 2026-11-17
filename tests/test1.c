/*
 * 测试1:
 * 测试方法: 主机每个帧发送一次
 * 测试目标: 每一帧都得到正确响应
 * 测试目的: 最简单测试
 *
 * 编译：
 *   cc -std=gnu11 -Wall -Wextra -O2 test1.c frame_io.c iap_flow.c -o test1
 *
 * 运行：
 *   ./test1 [/dev/ttyUSB0]
 *
 * 流程：0x01 启动 -> 0x02 版本 -> 0x03 文件大小 -> 0x04 擦除
 *       -> 0x05 一包数据 -> 0x06 总包数 -> 0x07 总校验 -> 0x08 完成重启
 * 每一步都校验应答帧的 CMD/SUB/LEN/DATA/CRC。
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
    uint8_t got_ver[2] = {0x00, 0x00};
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

    printf("IAP test1 on %s, payload=%u, crc=0x%04X\n",
           dev, PAYLOAD_LEN, crc);

    if (report("start upgrade", iap_start(), NULL, 0U) != 0) {
        goto out;
    }
    if (report("get version", iap_version(ver, got_ver), got_ver, 2U) != 0) {
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
    if (report("finish/reboot", iap_finish(), NULL, 0U) != 0) {
        goto out;
    }

    printf("TEST1 PASS\n");
    ret = 0;

out:
    if (ret != 0) {
        printf("TEST1 FAIL\n");
    }
    serial_close();
    return ret;
}
