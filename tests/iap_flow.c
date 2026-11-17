#include <string.h>

#include "frame_io.h"
#include "iap_flow.h"

static int expect(uint8_t sub, const uint8_t *data, uint8_t data_len, uint8_t *got)
{
    return recv_ack(sub, data, data_len, NORMAL_TIMEOUT, got, NULL);
}

int iap_start(void)
{
    if (send_cmd(0x01, NULL, 0U) != 0) {
        return ACK_ERR_IO;
    }
    return expect(0x01, NULL, 0U, NULL);
}

int iap_version(const uint8_t ver[2], uint8_t got[2])
{
    if (send_cmd(0x02, ver, 2U) != 0) {
        return ACK_ERR_IO;
    }
    return expect(0x02, NULL, 2U, got);
}

int iap_size(uint32_t file_size)
{
    uint8_t d[4] = {
        (uint8_t)(file_size >> 24),
        (uint8_t)(file_size >> 16),
        (uint8_t)(file_size >> 8),
        (uint8_t)file_size,
    };

    if (send_cmd(0x03, d, (uint8_t)sizeof(d)) != 0) {
        return ACK_ERR_IO;
    }
    return expect(0x03, d, (uint8_t)sizeof(d), NULL);
}

int iap_erase(int *keepalive_count)
{
    uint8_t frame[FRAME_MAX_SIZE];
    int count = 0;
    int n;

    if (send_cmd(0x04, NULL, 0U) != 0) {
        return ACK_ERR_IO;
    }
    for (;;) {
        n = read_frame(frame, sizeof(frame), NORMAL_TIMEOUT);
        if (n <= 0) {
            return (n == 0) ? ACK_ERR_TIMEOUT : ACK_ERR_IO;
        }
        if (frame[3] != CMD_SLAVE) {
            return ACK_ERR_CMD;
        }
        if (frame[4] == 0x04) {
            count++;
            continue;
        }
        if (frame[4] == 0x05) {
            break;
        }
        return ACK_ERR_CMD;
    }

    if (keepalive_count != NULL) {
        *keepalive_count = count;
    }
    return ACK_OK;
}

int iap_packet(uint16_t id, const uint8_t *data, uint8_t data_len)
{
    uint8_t d[2 + IAP_DATA_MAX];

    if (data_len > IAP_DATA_MAX) {
        return ACK_ERR_LEN;
    }
    d[0] = (uint8_t)(id >> 8);
    d[1] = (uint8_t)id;
    if (data_len != 0U) {
        memcpy(d + 2, data, data_len);
    }

    if (send_cmd(0x05, d, (uint8_t)(data_len + 2U)) != 0) {
        return ACK_ERR_IO;
    }
    return expect(0x06, d, 2U, NULL);
}

int iap_packet_count(uint16_t count)
{
    uint8_t d[2] = {(uint8_t)(count >> 8), (uint8_t)count};

    if (send_cmd(0x06, d, 2U) != 0) {
        return ACK_ERR_IO;
    }
    return expect(0x07, d, 2U, NULL);
}

int iap_crc(uint16_t crc)
{
    uint8_t d[2] = {(uint8_t)(crc >> 8), (uint8_t)crc};

    if (send_cmd(0x07, d, 2U) != 0) {
        return ACK_ERR_IO;
    }
    return expect(0x08, d, 2U, NULL);
}

int iap_finish(void)
{
    if (send_cmd(0x08, NULL, 0U) != 0) {
        return ACK_ERR_IO;
    }
    return expect(0x09, NULL, 0U, NULL);
}
