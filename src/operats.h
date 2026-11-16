#ifndef OPERATS_H
#define OPERATS_H

#include <stdint.h>

#include "parse-error.h"

/* 主机 0xF1 各二级指令对应的处理动作，frame 为整帧，len 含 CRC */
parse_err_t ack_upgrade(const uint8_t *frame, uint16_t len);
parse_err_t ack_version(const uint8_t *frame, uint16_t len);
parse_err_t ack_file_size(const uint8_t *frame, uint16_t len);
parse_err_t ack_eraseing(const uint8_t *frame, uint16_t len);
parse_err_t ack_packet_id(const uint8_t *frame, uint16_t len);
parse_err_t ack_number_of_packets(const uint8_t *frame, uint16_t len);
parse_err_t ack_all_data_crc(const uint8_t *frame, uint16_t len);
parse_err_t ready_rebot(const uint8_t *frame, uint16_t len);

#endif /* OPERATS_H */
