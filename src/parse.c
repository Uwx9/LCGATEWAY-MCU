#include "parse.h"

#define HEAD      0x40
#define FLAG_WR   0x80
#define FLAG_ACK  0x40

typedef enum {
    S_HEAD = 0, S_RW, S_LEN, S_CMD, S_SUB, S_DATA, S_CRC_L, S_CRC_H
} parse_state_t;

static struct {
    parse_state_t st;
    uint8_t  buf[FRAME_MAX];
    uint16_t idx;
    uint8_t  len;      /* LEN 字段 */
    uint8_t  need;     /* 还差多少个数据字节 */
} g_rx;

void process_all_available()
{

}
static void reset(void)
{
    g_rx.st = S_HEAD;
    g_rx.idx = 0;
    g_rx.len = 0;
    g_rx.need = 0;
}

/* 出错时重同步：如果当前字节正好是帧头，就把它当新帧的开头 */
static void restart(uint8_t b)
{
    reset();
    if (b == FRAME_HEAD) {
        p.buf[0] = b;
        p.idx = 1;
        p.st = S_RW;
    }
}

void parse_init(void)
{
    reset();
}

static void parse_feed(uint8_t b)
{
    switch (g_rx.st) {
    case S_HEAD:
        if (b == FRAME_HEAD) {
            g_rx.buf[0] = b;
            g_rx.idx = 1;
            g_rx.st = S_RW;
        }
        break;

    case S_RW:
        if (b != FRAME_RW) {
            restart(b);
            break;
        }
        g_rx.buf[g_rx.idx++] = b;
        g_rx.st = S_LEN;
        break;

    case S_LEN:
        /* LEN 至少包含 5 字节头，且整帧不能超过缓冲 */
        if (b < 5 || (uint16_t)b + 2 > FRAME_MAX) {
            restart(b);
            break;
        }
        g_rx.buf[g_rx.idx++] = b;
        g_rx.len  = b;
        g_rx.need = (uint8_t)(b - 5);   /* 关键修正 */
        g_rx.st   = S_CMD;
        break;

    case S_CMD:
        if (b != CMD_HOST && b != CMD_SLAVE) {
            restart(b);
            break;
        }
        g_rx.buf[g_rx.idx++] = b;
        g_rx.st = S_SUB;
        break;

    case S_SUB:
        g_rx.buf[g_rx.idx++] = b;
        g_rx.st = g_rx.need ? S_DATA : S_CRC_L;
        break;

    case S_DATA:
        g_rx.buf[g_rx.idx++] = b;
        if (--g_rx.need == 0) {
            g_rx.st = S_CRC_L;
        }
        break;

    case S_CRC_L:
        g_rx.buf[g_rx.idx++] = b;
        g_rx.st = S_CRC_H;
        break;

    case S_CRC_H: {
        g_rx.buf[g_rx.idx++] = b;

        uint16_t got = (uint16_t)g_rx.buf[g_rx.len]
                       | ((uint16_t)g_rx.buf[g_rx.len + 1] << 8);
        if (crc16(g_rx.buf, g_rx.len) == got) {
            on_frame(g_rx.buf, (uint16_t)(g_rx.len + 2));
        }
        reset();
        break;
    }
    }
}

/* 从环形缓冲的 [from, to) 区间喂给状态机，自动处理回绕 */
static void g_rx.rse_feed_ring(const uint8_t *ring, uint16_t ring_size,
                               uint16_t from, uint16_t to)
{
    while (from != to) {
        parse_feed(ring[from]);
        if (++from == ring_size) {
            from = 0;
        }
    }
}
