#define HEAD      0x40
#define FLAG_WR   0x80
#define FLAG_ACK  0x40

static struct {
    uint8_t st, buf[RXBUF_SIZE], idx, need;
} rx;

typedef enum { S_HEAD, S_RW, S_LEN, S_C1, S_C2, S_DATA, S_CRC_L, S_CRC_H } st_t;

void process_all_available()
{
	
}

void rx_feed(uint8_t b)
{
    switch (rx.st) {
    case S_HEAD:
        if (b == HEAD) { rx.buf[0] = b; rx.idx = 1; rx.st = S_RW; }
        break;

    case S_RW:   rx.buf[rx.idx++] = b; rx.st = S_LEN;  break;

    case S_LEN:
        rx.buf[rx.idx++] = b;
        rx.need = b;                       /* 1 字节长度，天然 ≤255，无需越界检查 */
        rx.st = S_C1;
        break;

    case S_C1:   rx.buf[rx.idx++] = b; rx.st = S_C2;   break;

    case S_C2:
        rx.buf[rx.idx++] = b;
        rx.st = rx.need ? S_DATA : S_CRC_L;
        break;

    case S_DATA:
        rx.buf[rx.idx++] = b;
        if (--rx.need == 0) rx.st = S_CRC_L;
        break;

    case S_CRC_L: rx.buf[rx.idx++] = b; rx.st = S_CRC_H; break;

    case S_CRC_H:
        rx.buf[rx.idx++] = b;
        {
            uint16_t got = rx.buf[rx.idx-2] | ((uint16_t)rx.buf[rx.idx-1] << 8);
            uint16_t cal = crc16(rx.buf, rx.idx - 2);   /* 含数据头 */
            if (cal == got) on_frame(rx.buf, rx.idx);
        }
        rx.st = S_HEAD;                    /* 成败都回找头 */
        break;
    }
}
