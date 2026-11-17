#ifndef FRAME_IO_H
#define FRAME_IO_H

#include <stddef.h>
#include <stdint.h>

#define FRAME_HEAD      0x40
#define FRAME_RW        0x06
#define CMD_HOST        0xF1
#define CMD_SLAVE       0xF2
#define FRAME_HDR_LEN   5U
#define FRAME_LEN_MAX   135U
#define FRAME_MAX_SIZE  (FRAME_LEN_MAX + 2U)

#define RX_BUF_SIZE     512U
#define NORMAL_TIMEOUT  2000

/* recv_ack 返回值，0 表示成功 */
#define ACK_OK           0
#define ACK_ERR_TIMEOUT  (-1)
#define ACK_ERR_IO       (-2)
#define ACK_ERR_CMD      (-3)
#define ACK_ERR_LEN      (-4)
#define ACK_ERR_DATA     (-5)

/* 打开串口并设为 115200 8N1 原始模式，成功返回 0 */
int serial_open(const char *dev);
void serial_close(void);

uint16_t crc16(const uint8_t *data, size_t len);
void dump_hex(const uint8_t *data, size_t len);

int send_cmd(uint8_t sub, const uint8_t *data, uint8_t data_len);

/* 从串口读完整的一帧，返回帧长；0 超时，-1 错误 */
int read_frame(uint8_t *frame, size_t cap, int timeout_ms);

/*
 * 读一帧应答并校验 CMD/SUB/LEN/DATA，返回 ACK_OK 或 ACK_ERR_*。
 * data 为 NULL 时不比对内容，data_len 仍为期望长度。
 * got 非 NULL 时带回应答 payload，容量至少为 data_len。
 */
int recv_ack(uint8_t sub, const uint8_t *data, uint8_t data_len,
             int timeout_ms, uint8_t *got, uint8_t *got_len);

const char *ack_str(int err);

#endif
