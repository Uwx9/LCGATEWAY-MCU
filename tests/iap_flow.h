#ifndef IAP_FLOW_H
#define IAP_FLOW_H

#include <stdint.h>

#define IAP_DATA_MAX  128U

/* 每条命令返回 ACK_OK 或 ACK_ERR_*，不做任何打印 */
int iap_start(void);
int iap_version(const uint8_t ver[2], uint8_t got[2]);
int iap_size(uint32_t file_size);
int iap_erase(int *keepalive_count);
int iap_packet(uint16_t id, const uint8_t *data, uint8_t data_len);
int iap_packet_count(uint16_t count);
int iap_crc(uint16_t crc);
int iap_finish(void);

#endif
