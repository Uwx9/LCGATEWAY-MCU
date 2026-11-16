/*
 * 测试方法 每次主机传输<=buffersize大小的数据量并开头有一帧, 后面填充0或ff
 * buffersize应该至少256字节，一帧最长为137字节
 * 希望快点溢出就buffersize, 健壮性考虑就都测试一下
 * 让dmawrap快速增加，这样dma_total 和 read_total会快速溢出，然后dmawrap也会溢出
 * 统计dma_total和read_total和dam_wrap的溢出次数,对比dma_wrap溢出前后的avail值证明dmawrap溢出不影响
 * 在这个实验里，avail应该一直都是buffersize，因为我们一次传输buufersize大小的数据
 * 当然要求每一帧都正确处理
 *
 */

/*
 * 测试1:
 * 测试方法: 主机每个帧发送一次
 * 测试目标: 每一帧都得到正确响应
 * 测试目的: 最简单测试
 *
 * 编译：
 *   cc -std=gnu11 -Wall -Wextra -O2 test1.c -o test1
 *
 * 运行：
 *   ./test1 [/dev/ttyUSB0]
 *
 * 流程：0x01 启动 -> 0x02 版本 -> 0x03 文件大小 -> 0x04 擦除
 *       -> 0x05 一包数据 -> 0x06 总包数 -> 0x07 总校验 -> 0x08 完成重启
 * 每一步都校验应答帧的 CMD/SUB/LEN/DATA/CRC。
 */

#define _DEFAULT_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include <sys/select.h>

#define FRAME_HEAD      0x40
#define FRAME_RW        0x06
#define CMD_HOST        0xF1
#define CMD_SLAVE       0xF2
#define FRAME_HDR_LEN   5U
#define FRAME_LEN_MAX   135U
#define FRAME_MAX_SIZE  (FRAME_LEN_MAX + 2U)

#define RX_BUF_SIZE     512U
#define NORMAL_TIMEOUT  2000
#define ERASE_TIMEOUT   30000

#define PAYLOAD_LEN     100U

static int g_fd = -1;
static uint8_t g_rx[RX_BUF_SIZE];
static size_t g_rx_len;

static uint16_t crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFFU;
    size_t i;
    int bit;

    for (i = 0U; i < len; i++) {
        crc ^= data[i];
        for (bit = 0; bit < 8; bit++) {
            if ((crc & 0x0001U) != 0U) {
                crc = (uint16_t)((crc >> 1) ^ 0xA001U);
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

static int64_t now_ms(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void dump_hex(const uint8_t *data, size_t len)
{
    size_t i;

    for (i = 0U; i < len; i++) {
        printf("%02X ", data[i]);
    }
}

static int write_all(const uint8_t *data, size_t len)
{
    while (len > 0U) {
        ssize_t n = write(g_fd, data, len);

        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("write");
            return -1;
        }
        data += n;
        len -= (size_t)n;
    }
    return 0;
}

/* 从串口读完整的一帧，返回帧长；0 超时，-1 错误 */
static int read_frame(uint8_t *frame, size_t cap, int timeout_ms)
{
    int64_t deadline = now_ms() + timeout_ms;

    for (;;) {
        size_t drop = 0U;

        while ((drop < g_rx_len) && (g_rx[drop] != FRAME_HEAD)) {
            drop++;
        }
        if (drop != 0U) {
            memmove(g_rx, g_rx + drop, g_rx_len - drop);
            g_rx_len -= drop;
        }

        while (g_rx_len >= 3U) {
            uint8_t len;
            size_t total;

            if (g_rx[1] != FRAME_RW) {
                memmove(g_rx, g_rx + 1, --g_rx_len);
                continue;
            }
            len = g_rx[2];
            if ((len < FRAME_HDR_LEN) || (len > FRAME_LEN_MAX)) {
                memmove(g_rx, g_rx + 1, --g_rx_len);
                continue;
            }
            total = (size_t)len + 2U;
            if (g_rx_len < total) {
                break;
            }

            if (((uint16_t)g_rx[len] | ((uint16_t)g_rx[len + 1U] << 8)) ==
                crc16(g_rx, len)) {
                if (total > cap) {
                    return -1;
                }
                memcpy(frame, g_rx, total);
                memmove(g_rx, g_rx + total, g_rx_len - total);
                g_rx_len -= total;
                return (int)total;
            }

            /* CRC 错，丢一个字节重新同步 */
            memmove(g_rx, g_rx + 1, --g_rx_len);
        }

        {
            int64_t remain = deadline - now_ms();
            fd_set rfds;
            struct timeval tv;
            int ret;

            if (remain <= 0) {
                return 0;
            }
            FD_ZERO(&rfds);
            FD_SET(g_fd, &rfds);
            tv.tv_sec = (time_t)(remain / 1000);
            tv.tv_usec = (suseconds_t)((remain % 1000) * 1000);

            ret = select(g_fd + 1, &rfds, NULL, NULL, &tv);
            if (ret < 0) {
                if (errno == EINTR) {
                    continue;
                }
                perror("select");
                return -1;
            }
            if (ret == 0) {
                return 0;
            }

            {
                ssize_t n = read(g_fd, g_rx + g_rx_len, sizeof(g_rx) - g_rx_len);

                if (n < 0) {
                    if ((errno == EINTR) || (errno == EAGAIN)) {
                        continue;
                    }
                    perror("read");
                    return -1;
                }
                if (n == 0) {
                    return 0;
                }
                g_rx_len += (size_t)n;
            }
        }
    }
}

static size_t build_frame(uint8_t *out, uint8_t sub,
                          const uint8_t *data, uint8_t data_len)
{
    size_t len = FRAME_HDR_LEN + data_len;
    uint16_t crc;

    out[0] = FRAME_HEAD;
    out[1] = FRAME_RW;
    out[2] = (uint8_t)len;
    out[3] = CMD_HOST;
    out[4] = sub;
    if ((data != NULL) && (data_len != 0U)) {
        memcpy(out + FRAME_HDR_LEN, data, data_len);
    }

    crc = crc16(out, len);
    out[len] = (uint8_t)(crc & 0xFFU);
    out[len + 1U] = (uint8_t)(crc >> 8);

    return len + 2U;
}

static int send_cmd(uint8_t sub, const uint8_t *data, uint8_t data_len)
{
    uint8_t frame[FRAME_MAX_SIZE];
    size_t n = build_frame(frame, sub, data, data_len);

    return write_all(frame, n);
}

/* 读一帧并校验 CMD/SUB；data 可为 NULL，表示不关心内容 */
static int expect_ack(uint8_t sub, const uint8_t *data, uint8_t data_len,
                      int timeout_ms, const char *name)
{
    uint8_t frame[FRAME_MAX_SIZE];
    int n = read_frame(frame, sizeof(frame), timeout_ms);
    uint8_t got_len;

    if (n <= 0) {
        printf("FAIL %-28s: %s\n", name, (n == 0) ? "timeout" : "read error");
        return -1;
    }
    if (frame[3] != CMD_SLAVE || frame[4] != sub) {
        printf("FAIL %-28s: cmd=0x%02X sub=0x%02X\n", name, frame[3], frame[4]);
        printf("     frame: ");
        dump_hex(frame, (size_t)n);
        printf("\n");
        return -1;
    }

    got_len = (uint8_t)(frame[2] - FRAME_HDR_LEN);
    if (got_len != data_len) {
        printf("FAIL %-28s: len=%u (want %u)\n", name, got_len, data_len);
        return -1;
    }
    if ((data != NULL) && (data_len != 0U) &&
        (memcmp(frame + FRAME_HDR_LEN, data, data_len) != 0)) {
        printf("FAIL %-28s: data mismatch\n", name);
        printf("     got:   ");
        dump_hex(frame + FRAME_HDR_LEN, got_len);
        printf("\n     want:  ");
        dump_hex(data, data_len);
        printf("\n");
        return -1;
    }

    printf("PASS %-28s", name);
    if (got_len != 0U) {
        printf(" data=");
        dump_hex(frame + FRAME_HDR_LEN, got_len);
    }
    printf("\n");
    return 0;
}

static int serial_open(const char *dev)
{
    struct termios tio;
    int fd = open(dev, O_RDWR | O_NOCTTY | O_NONBLOCK);

    if (fd < 0) {
        perror(dev);
        return -1;
    }
    if (tcgetattr(fd, &tio) != 0) {
        perror("tcgetattr");
        close(fd);
        return -1;
    }

    cfmakeraw(&tio);
    tio.c_cflag |= (CLOCAL | CREAD);
    tio.c_cflag &= ~(CSTOPB | PARENB | CRTSCTS);
    tio.c_cc[VMIN] = 0;
    tio.c_cc[VTIME] = 0;
    cfsetispeed(&tio, B115200);
    cfsetospeed(&tio, B115200);

    if (tcsetattr(fd, TCSANOW, &tio) != 0) {
        perror("tcsetattr");
        close(fd);
        return -1;
    }
    tcflush(fd, TCIOFLUSH);
    return fd;
}

int main(int argc, char **argv)
{
    const char *dev = (argc > 1) ? argv[1] : "/dev/ttyUSB0";
    uint8_t payload[PAYLOAD_LEN];
    uint8_t ver[2] = {0x00, 0x02};
    uint8_t size[4] = {0x00, 0x00, 0x00, PAYLOAD_LEN};
    uint8_t pkt[2 + PAYLOAD_LEN];
    uint8_t count[2] = {0x00, 0x01};
    uint8_t crc_data[2];
    uint16_t total_crc;
    size_t i;
    int erasing = 0;
    int ret = 1;

    setvbuf(stdout, NULL, _IOLBF, 0);

    for (i = 0U; i < PAYLOAD_LEN; i++) {
        payload[i] = (uint8_t)(i * 7U + 3U);
    }
    total_crc = crc16(payload, PAYLOAD_LEN);
    pkt[0] = 0x00;
    pkt[1] = 0x00;
    memcpy(pkt + 2, payload, PAYLOAD_LEN);
    crc_data[0] = (uint8_t)(total_crc >> 8);
    crc_data[1] = (uint8_t)(total_crc & 0xFFU);

    g_fd = serial_open(dev);
    if (g_fd < 0) {
        return 1;
    }

    printf("IAP test1 on %s, payload=%u, crc=0x%04X\n",
           dev, PAYLOAD_LEN, total_crc);

    if (send_cmd(0x01, NULL, 0U) != 0) {
        goto out;
    }
    if (expect_ack(0x01, NULL, 0U, NORMAL_TIMEOUT, "start upgrade") != 0) {
        goto out;
    }

    if (send_cmd(0x02, ver, sizeof(ver)) != 0) {
        goto out;
    }
    if (expect_ack(0x02, NULL, 2U, NORMAL_TIMEOUT, "get version") != 0) {
        goto out;
    }

    if (send_cmd(0x03, size, sizeof(size)) != 0) {
        goto out;
    }
    if (expect_ack(0x03, size, sizeof(size), NORMAL_TIMEOUT, "file size") != 0) {
        goto out;
    }

    if (send_cmd(0x04, NULL, 0U) != 0) {
        goto out;
    }
    for (;;) {
        uint8_t frame[FRAME_MAX_SIZE];
        int n = read_frame(frame, sizeof(frame), NORMAL_TIMEOUT);

        if (n <= 0) {
            printf("FAIL %-28s: %s\n", "erase",
                   (n == 0) ? "timeout" : "read error");
            goto out;
        }
        if (frame[3] != CMD_SLAVE) {
            printf("FAIL %-28s: cmd=0x%02X\n", "erase", frame[3]);
            goto out;
        }
        if (frame[4] == 0x04) {
            erasing++;
            continue;
        }
        if (frame[4] == 0x05) {
            printf("PASS %-28s keepalive=%d\n", "erase", erasing);
            break;
        }
        printf("FAIL %-28s: unexpected sub=0x%02X\n", "erase", frame[4]);
        goto out;
    }

    if (send_cmd(0x05, pkt, (uint8_t)sizeof(pkt)) != 0) {
        goto out;
    }
    if (expect_ack(0x06, pkt, 2U, NORMAL_TIMEOUT, "send data id=0") != 0) {
        goto out;
    }

    if (send_cmd(0x06, count, sizeof(count)) != 0) {
        goto out;
    }
    if (expect_ack(0x07, count, sizeof(count), NORMAL_TIMEOUT, "packet count") != 0) {
        goto out;
    }

    if (send_cmd(0x07, crc_data, sizeof(crc_data)) != 0) {
        goto out;
    }
    if (expect_ack(0x08, crc_data, sizeof(crc_data), NORMAL_TIMEOUT, "total crc") != 0) {
        goto out;
    }

    if (send_cmd(0x08, NULL, 0U) != 0) {
        goto out;
    }
    if (expect_ack(0x09, NULL, 0U, NORMAL_TIMEOUT, "finish/reboot") != 0) {
        goto out;
    }

    printf("TEST1 PASS\n");
    ret = 0;

out:
    if (ret != 0) {
        printf("TEST1 FAIL\n");
    }
    close(g_fd);
    return ret;
}
