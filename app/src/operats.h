#ifndef OPERATS_H
#define OPERATS_H

#include <stdint.h>

#include "errcode.h"

/* 主机 IAP 指令（0x01~0x09）对应的处理动作，frame 为整帧，len 为整帧长度（含 CRC） */
parse_err_t ack_set_DO(const uint8_t *frame, uint16_t len);
parse_err_t ack_read_DI(const uint8_t *frame, uint16_t len);
parse_err_t ack_upgrade(const uint8_t *frame, uint16_t len);
parse_err_t ack_file_size(const uint8_t *frame, uint16_t len);
parse_err_t ack_eraseing(const uint8_t *frame, uint16_t len);
parse_err_t ack_packet_id(const uint8_t *frame, uint16_t len);
parse_err_t ack_number_of_packets(const uint8_t *frame, uint16_t len);
parse_err_t ack_all_data_crc(const uint8_t *frame, uint16_t len);
parse_err_t ready_rebot(const uint8_t *frame, uint16_t len);

/* 一帧处理前后调用：没有实际应答时由 iap_frame_end() 补一帧 0x8F 错误帧 */
void iap_frame_begin(void);
parse_err_t iap_frame_end(uint8_t cmd, parse_err_t e);

#ifdef TEST5
/* 0x7E 测试命令：预置/回读回绕计数 */
parse_err_t test_cmd(const uint8_t *frame, uint16_t len);
#endif

#endif /* OPERATS_H */
