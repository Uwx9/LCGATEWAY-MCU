#include <string.h>

#include "frame_io.h"
#include "iap_flow.h"

static int expect(uint8_t cmd, const uint8_t *data, uint8_t data_len, uint8_t *got)
{
    return recv_ack(cmd, data, data_len, NORMAL_TIMEOUT, got, NULL);
}

int iap_set_do(uint8_t id, uint8_t status, uint8_t *result)
{
    uint8_t d[2] = {id, status};
    uint8_t got = 0xFFU;
    int e;

    if (send_cmd(IAP_CMD_SET_DO, d, (uint8_t)sizeof(d)) != 0) {
        return ACK_ERR_IO;
    }
    e = expect(IAP_ACK(IAP_CMD_SET_DO), NULL, 1U, &got);
    if ((e == ACK_OK) && (result != NULL)) {
        *result = got;
    }
    return e;
}

int iap_start(const uint8_t ver[2], uint8_t *can_upgrade)
{
    uint8_t got = 0U;
    int e;

    if (send_cmd(IAP_CMD_START, ver, 2U) != 0) {
        return ACK_ERR_IO;
    }
    e = expect(IAP_ACK(IAP_CMD_START), NULL, 1U, &got);
    if ((e == ACK_OK) && (can_upgrade != NULL)) {
        *can_upgrade = got;
    }
    return e;
}

int iap_size(uint32_t file_size)
{
    uint8_t d[4] = {
        (uint8_t)(file_size >> 24),
        (uint8_t)(file_size >> 16),
        (uint8_t)(file_size >> 8),
        (uint8_t)file_size,
    };

    if (send_cmd(IAP_CMD_FILE_SIZE, d, (uint8_t)sizeof(d)) != 0) {
        return ACK_ERR_IO;
    }
    return expect(IAP_ACK(IAP_CMD_FILE_SIZE), d, (uint8_t)sizeof(d), NULL);
}

int iap_erase(int *keepalive_count)
{
    uint8_t frame[FRAME_MAX_SIZE];
    int count = 0;
    int n;

    if (send_cmd(IAP_CMD_ERASE, NULL, 0U) != 0) {
        return ACK_ERR_IO;
    }
    for (;;) {
        n = read_frame(frame, sizeof(frame), NORMAL_TIMEOUT);
        if (n <= 0) {
            return (n == 0) ? ACK_ERR_TIMEOUT : ACK_ERR_IO;
        }
        if (frame[2] != IAP_ACK(IAP_CMD_ERASE)) {
            return ACK_ERR_CMD;
        }
        if ((frame[1] - FRAME_OVERHEAD) != 1U) {
            return ACK_ERR_LEN;
        }

        count++;
        if (frame[FRAME_DATA_OFF] >= 100U) {
            break;
        }
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

    if (send_cmd(IAP_CMD_DATA, d, (uint8_t)(data_len + 2U)) != 0) {
        return ACK_ERR_IO;
    }
    return expect(IAP_ACK(IAP_CMD_DATA), d, 2U, NULL);
}

int iap_packet_count(uint16_t count)
{
    uint8_t d[2] = {(uint8_t)(count >> 8), (uint8_t)count};

    if (send_cmd(IAP_CMD_PACKETS, d, 2U) != 0) {
        return ACK_ERR_IO;
    }
    return expect(IAP_ACK(IAP_CMD_PACKETS), d, 2U, NULL);
}

int iap_crc(uint16_t crc)
{
    uint8_t d[2] = {(uint8_t)(crc >> 8), (uint8_t)crc};

    if (send_cmd(IAP_CMD_TOTAL_CRC, d, 2U) != 0) {
        return ACK_ERR_IO;
    }
    return expect(IAP_ACK(IAP_CMD_TOTAL_CRC), d, 2U, NULL);
}

int iap_finish(void)
{
    if (send_cmd(IAP_CMD_FINISH, NULL, 0U) != 0) {
        return ACK_ERR_IO;
    }
    return expect(IAP_ACK(IAP_CMD_FINISH), NULL, 0U, NULL);
}
