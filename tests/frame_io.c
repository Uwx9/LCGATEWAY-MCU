#define _DEFAULT_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include <sys/select.h>

#include "frame_io.h"

static int g_fd = -1;
static uint8_t g_rx[RX_BUF_SIZE];
static size_t g_rx_len;

uint16_t crc16(const uint8_t *data, size_t len)
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

void dump_hex(const uint8_t *data, size_t len)
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

int read_frame(uint8_t *frame, size_t cap, int timeout_ms)
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

        while (g_rx_len >= 2U) {
            uint8_t len;
            size_t total;

            len = g_rx[1];
            if ((len < FRAME_LEN_MIN) || (len > FRAME_LEN_MAX)) {
                memmove(g_rx, g_rx + 1, --g_rx_len);
                continue;
            }
            total = (size_t)len;
            if (g_rx_len < total) {
                break;
            }

            if (((uint16_t)g_rx[len - 2U] |
                 ((uint16_t)g_rx[len - 1U] << 8)) ==
                crc16(g_rx, (size_t)(len - 2U))) {
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

static size_t build_frame(uint8_t *out, uint8_t cmd,
                          const uint8_t *data, uint8_t data_len)
{
    size_t total = FRAME_OVERHEAD + data_len;
    uint16_t crc;

    out[0] = FRAME_HEAD;
    out[1] = (uint8_t)total;
    out[2] = cmd;
    if ((data != NULL) && (data_len != 0U)) {
        memcpy(out + FRAME_DATA_OFF, data, data_len);
    }

    crc = crc16(out, total - 2U);
    out[total - 2U] = (uint8_t)(crc & 0xFFU);
    out[total - 1U] = (uint8_t)(crc >> 8);

    return total;
}

int send_cmd(uint8_t cmd, const uint8_t *data, uint8_t data_len)
{
    uint8_t frame[FRAME_MAX_SIZE];
    size_t n = build_frame(frame, cmd, data, data_len);

    return write_all(frame, n);
}

int serial_open(const char *dev)
{
    struct termios tio;

    g_fd = open(dev, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (g_fd < 0) {
        perror(dev);
        return -1;
    }
    if (tcgetattr(g_fd, &tio) != 0) {
        perror("tcgetattr");
        close(g_fd);
        g_fd = -1;
        return -1;
    }

    cfmakeraw(&tio);
    tio.c_cflag |= (CLOCAL | CREAD);
    tio.c_cflag &= ~(CSTOPB | PARENB | CRTSCTS);
    tio.c_cc[VMIN] = 0;
    tio.c_cc[VTIME] = 0;
    cfsetispeed(&tio, B115200);
    cfsetospeed(&tio, B115200);

    if (tcsetattr(g_fd, TCSANOW, &tio) != 0) {
        perror("tcsetattr");
        close(g_fd);
        g_fd = -1;
        return -1;
    }
    tcflush(g_fd, TCIOFLUSH);
    g_rx_len = 0U;
    return 0;
}

void serial_close(void)
{
    if (g_fd >= 0) {
        close(g_fd);
        g_fd = -1;
    }
}

int recv_ack(uint8_t cmd, const uint8_t *data, uint8_t data_len,
             int timeout_ms, uint8_t *got, uint8_t *got_len)
{
    uint8_t frame[FRAME_MAX_SIZE];
    int n = read_frame(frame, sizeof(frame), timeout_ms);
    uint8_t len;

    if (n <= 0) {
        return (n == 0) ? ACK_ERR_TIMEOUT : ACK_ERR_IO;
    }
    if (frame[2] != cmd) {
        return ACK_ERR_CMD;
    }

    len = (uint8_t)(frame[1] - FRAME_OVERHEAD);
    if (len != data_len) {
        return ACK_ERR_LEN;
    }
    if ((data != NULL) && (data_len != 0U) &&
        (memcmp(frame + FRAME_DATA_OFF, data, data_len) != 0)) {
        return ACK_ERR_DATA;
    }

    if (got != NULL) {
        memcpy(got, frame + FRAME_DATA_OFF, len);
    }
    if (got_len != NULL) {
        *got_len = len;
    }
    return ACK_OK;
}

const char *ack_str(int err)
{
    switch (err) {
    case ACK_OK:
        return "ok";
    case ACK_ERR_TIMEOUT:
        return "timeout";
    case ACK_ERR_IO:
        return "io error";
    case ACK_ERR_CMD:
        return "cmd/sub mismatch";
    case ACK_ERR_LEN:
        return "len mismatch";
    case ACK_ERR_DATA:
        return "data mismatch";
    default:
        return "unknown error";
    }
}
