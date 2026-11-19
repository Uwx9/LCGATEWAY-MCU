#ifndef FRAME_IO_H
#define FRAME_IO_H

#include <stddef.h>
#include <stdint.h>

#include "errcode.h"

#define FRAME_HEAD      IAP_FRAME_HEAD
#define FRAME_DATA_OFF  IAP_FRAME_DATA_OFF
#define FRAME_OVERHEAD  IAP_FRAME_OVERHEAD
#define FRAME_LEN_MIN   IAP_FRAME_LEN_MIN
#define FRAME_DATA_MAX  IAP_FRAME_DATA_MAX
#define FRAME_LEN_MAX   IAP_FRAME_LEN_MAX
#define FRAME_MAX_SIZE  IAP_FRAME_LEN_MAX

/* 主机指令、IAP_ACK()、IAP_ACK_ERR、IAP_ERR_* 见 common/errcode.h */
#define TEST_CMD        0x7EU

#define RX_BUF_SIZE     512U
#define NORMAL_TIMEOUT  2000

/* ACK_* 主机本地返回码见 common/errcode.h */

/* 打开串口并设为 115200 8N1 原始模式，成功返回 0 */
int serial_open(const char *dev);
void serial_close(void);

uint16_t crc16(const uint8_t *data, size_t len);
void dump_hex(const uint8_t *data, size_t len);

int send_cmd(uint8_t cmd, const uint8_t *data, uint8_t data_len);

/* 从串口读完整的一帧，返回帧长；0 超时，-1 错误 */
int read_frame(uint8_t *frame, size_t cap, int timeout_ms);

/*
 * 读一帧应答并校验 CMD/LEN/DATA，返回 ACK_OK 或 ACK_ERR_*。
 * data 为 NULL 时不比对内容，data_len 仍为期望长度。
 * got 非 NULL 时带回应答 payload，容量至少为 data_len。
 */
int recv_ack(uint8_t cmd, const uint8_t *data, uint8_t data_len,
             int timeout_ms, uint8_t *got, uint8_t *got_len);

/* frame 是 0x8F 错误帧时打印并返回 1，否则返回 0 */
int ack_is_error(const uint8_t *frame, int len);

/* 最近一次 0x8F 错误帧里的 err_code */
uint8_t ack_last_err_code(void);

const char *ack_str(int err);

#endif
