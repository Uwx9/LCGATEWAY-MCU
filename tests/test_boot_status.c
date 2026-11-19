/*
 * 查询设备当前是在 bootloader 下载模式还是 app 运行中。
 *
 * 用法:
 *   ./test_boot_status [/dev/ttyUSB0]
 *
 * 0x0A 只有 bootloader 下载器实现；app 会丢弃该命令，因此超时后用
 * 0x03 + 版本 0.0 再确认一次 app 是否在运行。
 */

#include <stdint.h>
#include <stdio.h>

#include "frame_io.h"
#include "iap_flow.h"

int main(int argc, char **argv)
{
    const char *dev = (argc > 1) ? argv[1] : "/dev/ttyUSB0";
    uint8_t mode = 0U;
    int e;

    if (serial_open(dev) != 0) {
        return 1;
    }

    e = iap_boot_status(&mode);
    if (e == ACK_OK) {
        if (mode == 1U) {
            printf("bootloader download mode\n");
        } else {
            printf("unknown boot status: %u\n", (unsigned)mode);
        }
        serial_close();
        return 0;
    }

    if (e == ACK_ERR_TIMEOUT) {
        uint8_t ver[2] = {0U, 0U};
        uint8_t can_upgrade = 0xFFU;

        e = iap_start(ver, &can_upgrade);
        if (e == ACK_OK) {
            if (can_upgrade == 0U) {
                printf("app running (0x03 can_upgrade=0)\n");
            } else {
                /* 目标版本 0.0 仍返回 1：只可能是 boot 下载器（旧 boot 无 0x0A） */
                printf("bootloader download mode (0x03 fallback)\n");
            }
            serial_close();
            return 0;
        }
        printf("no response (app not running or wrong port)\n");
        serial_close();
        return 1;
    }

    printf("status query failed: %s\n", ack_str(e));
    serial_close();
    return 1;
}
