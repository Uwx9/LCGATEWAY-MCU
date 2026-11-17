#include "FreeRTOS.h"
#include "task.h"

#include <string.h>

#include "gd32f3x0.h"

#include "memmap.h"
#include "operats.h"
#include "parse.h"
#include "version.h"

#define USART0_TDATA_ADDRESS   ((uint32_t)&USART_TDATA(USART0))

#define FLASH_PAGE_SIZE        1024U
#define DATA_PACKET_MAX        128U
#define ERASE_ACK_PAGES        8U      /* 擦除时按页数近似 500ms 发一次“擦除中” */
#define TX_TIMEOUT_MS          100U

/* DMA TX 完成中断置位 */
__IO FlagStatus g_transfer_complete = RESET;

typedef struct {
    uint8_t  active;
    uint8_t  size_ok;
    uint8_t  erased;
    uint8_t  crc_ok;
    uint8_t  error;
    uint8_t  have_last;
    uint8_t  word_len;
    uint16_t new_version;
    uint16_t declared_packets;
    uint16_t recv_packets;
    uint16_t last_packet_id;
    uint32_t file_size;
    uint32_t base_addr;
    uint32_t write_addr;
    uint32_t prog_bytes;
    uint32_t word;
} iap_ctx_t;

static iap_ctx_t s_iap;
static uint8_t s_tx[32];

/* 当前固件在 flash 中占用的末尾，向上对齐到 1KB 页 */
static uint32_t upgrade_base_addr(void)
{
    uint32_t used_end = memmap_image_end();

    return (used_end + (FLASH_PAGE_SIZE - 1U)) & ~(FLASH_PAGE_SIZE - 1U);
}

static uint32_t free_flash_size(void)
{
    uint32_t base = upgrade_base_addr();
    uint32_t end  = memmap_flash_end();

    return (end > base) ? (end - base) : 0U;
}

static void fmc_clear_flags(void)
{
    fmc_flag_clear(FMC_FLAG_END | FMC_FLAG_PGERR | FMC_FLAG_WPERR);
}

/* 用 DMA CH1 把一帧搬到 USART0，等待 FTF 中断确认发送完成 */
static parse_err_t tx_dma(const uint8_t *buf, uint16_t len)
{
    dma_parameter_struct dma_init_struct;
    TickType_t start;
    parse_err_t e = ERR_OK;

    dma_channel_disable(DMA_CH1);
    dma_deinit(DMA_CH1);
    dma_struct_para_init(&dma_init_struct);

    dma_init_struct.direction    = DMA_MEMORY_TO_PERIPHERAL;
    dma_init_struct.memory_addr  = (uint32_t)buf;
    dma_init_struct.memory_inc   = DMA_MEMORY_INCREASE_ENABLE;
    dma_init_struct.memory_width = DMA_MEMORY_WIDTH_8BIT;
    dma_init_struct.number       = len;
    dma_init_struct.periph_addr  = USART0_TDATA_ADDRESS;
    dma_init_struct.periph_inc   = DMA_PERIPH_INCREASE_DISABLE;
    dma_init_struct.periph_width = DMA_PERIPHERAL_WIDTH_8BIT;
    dma_init_struct.priority     = DMA_PRIORITY_ULTRA_HIGH;
    dma_init(DMA_CH1, &dma_init_struct);

    dma_circulation_disable(DMA_CH1);
    dma_memory_to_memory_disable(DMA_CH1);
    dma_interrupt_enable(DMA_CH1, DMA_INT_FTF);
    dma_interrupt_flag_clear(DMA_CH1, DMA_INT_FLAG_G);

    g_transfer_complete = RESET;
    dma_channel_enable(DMA_CH1);

    start = xTaskGetTickCount();
    while (g_transfer_complete == RESET) {
        if ((xTaskGetTickCount() - start) > pdMS_TO_TICKS(TX_TIMEOUT_MS)) {
            e = ERR_IAP_TX;
            break;
        }
        vTaskDelay(1);
    }
    dma_channel_disable(DMA_CH1);

    return e;
}

/* 组 0xF2 应答帧：LEN 从 HEAD 到数据末尾，CRC 覆盖 LEN 个字节 */
static parse_err_t send_ack(uint8_t sub_cmd, const uint8_t *data, uint8_t data_len)
{
    uint16_t len = (uint16_t)(FRAME_HDR_LEN + data_len);
    uint16_t crc;

    s_tx[0] = FRAME_HEAD;
    s_tx[1] = FRAME_RW;
    s_tx[2] = (uint8_t)len;
    s_tx[3] = CMD_SLAVE;
    s_tx[4] = sub_cmd;
    if ((data != NULL) && (data_len != 0U)) {
        memcpy(&s_tx[FRAME_HDR_LEN], data, data_len);
    }

    crc = CrcValueCalc(s_tx, len);
    s_tx[len]      = (uint8_t)(crc & 0xFFU);
    s_tx[len + 1U] = (uint8_t)(crc >> 8);

    return tx_dma(s_tx, (uint16_t)(len + 2U));
}

static parse_err_t ack_erase_ok(void)
{
    return send_ack(0x05, NULL, 0U);
}

/* 把不足 4 字节的尾字补 0xFF 后写入 flash */
static void flash_flush_word(void)
{
    uint32_t word;

    if (s_iap.word_len == 0U) {
        return;
    }
    word = s_iap.word;
    while (s_iap.word_len < 4U) {
        word |= 0xFFU << (8U * s_iap.word_len);
        s_iap.word_len++;
    }
    if (fmc_word_program(s_iap.write_addr, word) != FMC_READY) {
        s_iap.error = 1U;
    }
    s_iap.write_addr += 4U;
    s_iap.word = 0U;
    s_iap.word_len = 0U;
}

/* 逐字节累加成 32 位字再写 flash，不要求包长是 4 的倍数 */
static void flash_write(const uint8_t *data, uint16_t len)
{
    uint16_t i;

    for (i = 0U; i < len; i++) {
        s_iap.word |= (uint32_t)data[i] << (8U * s_iap.word_len);
        s_iap.word_len++;
        if (s_iap.word_len == 4U) {
            if (fmc_word_program(s_iap.write_addr, s_iap.word) != FMC_READY) {
                s_iap.error = 1U;
            }
            s_iap.write_addr += 4U;
            s_iap.word = 0U;
            s_iap.word_len = 0U;
        }
    }
}

/* 整包数据 CRC16，长度可能超过 64KB，不能直接用 uint16 的 CrcValueCalc */
static uint16_t crc16_flash(uint32_t addr, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)addr;
    uint16_t crc = 0xFFFFU;
    uint32_t i;

    for (i = 0U; i < len; i++) {
        uint8_t bit;

        crc ^= p[i];
        for (bit = 0U; bit < 8U; bit++) {
            if ((crc & 0x0001U) != 0U) {
                crc = (uint16_t)((crc >> 1) ^ 0xA001U);
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

/* 0x01 启动升级：复位升级上下文，回 0xF2 0x01 */
parse_err_t ack_upgrade(const uint8_t *frame, uint16_t len)
{
    (void)frame;
    (void)len;

    memset(&s_iap, 0, sizeof(s_iap));
    s_iap.active     = 1U;
    s_iap.base_addr  = upgrade_base_addr();
    s_iap.write_addr = s_iap.base_addr;

    return send_ack(0x01, NULL, 0U);
}

/* 0x02 发送版本号：保存主机版本，回当前原版本号 */
parse_err_t ack_version(const uint8_t *frame, uint16_t len)
{
    uint8_t data_len = (uint8_t)(frame[2] - FRAME_HDR_LEN);
    uint8_t out[2];

    (void)len;

    if (data_len < 2U) {
        return ERR_IAP_PARAM;
    }

    s_iap.new_version = (uint16_t)(((uint16_t)frame[5] << 8) | frame[6]);

    out[0] = g_fw_version_num.major;
    out[1] = g_fw_version_num.minor;
    return send_ack(0x02, out, 2U);
}

/* 0x03 发送文件大小：检查剩余 flash 是否放得下，回 0xF2 0x03 */
parse_err_t ack_file_size(const uint8_t *frame, uint16_t len)
{
    uint8_t data_len = (uint8_t)(frame[2] - FRAME_HDR_LEN);
    uint32_t size;
    uint8_t out[4];
    parse_err_t e;

    (void)len;

    if (s_iap.active == 0U) {
        return ERR_IAP_STATE;
    }
    if (data_len < 4U) {
        return ERR_IAP_PARAM;
    }

    size = ((uint32_t)frame[5] << 24) | ((uint32_t)frame[6] << 16) |
           ((uint32_t)frame[7] << 8)  | (uint32_t)frame[8];

    s_iap.file_size = size;
    s_iap.size_ok = ((size != 0U) && (size <= free_flash_size())) ? 1U : 0U;

    if (s_iap.size_ok != 0U) {
        out[0] = (uint8_t)(size >> 24);
        out[1] = (uint8_t)(size >> 16);
        out[2] = (uint8_t)(size >> 8);
        out[3] = (uint8_t)size;
    } else {
        out[0] = 0U;
        out[1] = 0U;
        out[2] = 0U;
        out[3] = 0U;
    }

    e = send_ack(0x03, out, 4U);
    if (e != ERR_OK) {
        return e;
    }
    return (s_iap.size_ok != 0U) ? ERR_OK : ERR_IAP_SIZE;
}

/* 0x04 开始擦除：擦除 [镜像末尾对齐 1KB, flash 末尾)，期间回“擦除中” */
parse_err_t ack_eraseing(const uint8_t *frame, uint16_t len)
{
    uint32_t addr;
    uint32_t pages = 0U;
    parse_err_t e;

    (void)frame;
    (void)len;

    if (s_iap.active == 0U) {
        return ERR_IAP_STATE;
    }
    if (s_iap.size_ok == 0U) {
        return ERR_IAP_SIZE;
    }

    e = send_ack(0x04, NULL, 0U);
    if (e != ERR_OK) {
        return e;
    }

    for (addr = s_iap.base_addr; addr < memmap_flash_end(); addr += FLASH_PAGE_SIZE) {
        fmc_unlock();
        fmc_clear_flags();
        if (fmc_page_erase(addr) != FMC_READY) {
            fmc_lock();
            s_iap.error = 1U;
            return ERR_IAP_FLASH;
        }
        fmc_lock();

        pages++;
        if (pages >= ERASE_ACK_PAGES) {
            pages = 0U;
            e = send_ack(0x04, NULL, 0U);
            if (e != ERR_OK) {
                return e;
            }
        }
    }

    s_iap.erased     = 1U;
    s_iap.write_addr = s_iap.base_addr;
    s_iap.prog_bytes = 0U;
    s_iap.recv_packets = 0U;
    s_iap.have_last  = 0U;
    s_iap.word       = 0U;
    s_iap.word_len   = 0U;

    return ack_erase_ok();
}

/* 0x05 发送数据：包号(2 字节, 高在前) + 最多 128 字节数据 */
parse_err_t ack_packet_id(const uint8_t *frame, uint16_t len)
{
    uint8_t data_len = (uint8_t)(frame[2] - FRAME_HDR_LEN);
    uint16_t id;
    uint16_t payload_len;
    uint8_t out[2];
    parse_err_t e;

    (void)len;

    if ((s_iap.active == 0U) || (s_iap.erased == 0U)) {
        return ERR_IAP_STATE;
    }
    if (s_iap.size_ok == 0U) {
        return ERR_IAP_SIZE;
    }
    if (data_len < 2U) {
        return ERR_IAP_PARAM;
    }

    payload_len = (uint16_t)(data_len - 2U);
    if (payload_len > DATA_PACKET_MAX) {
        return ERR_IAP_PARAM;
    }

    id = (uint16_t)(((uint16_t)frame[5] << 8) | frame[6]);

    /* 重发的包只回 ACK，不重复写 flash */
    if ((s_iap.have_last != 0U) && (id == s_iap.last_packet_id)) {
        out[0] = frame[5];
        out[1] = frame[6];
        return send_ack(0x06, out, 2U);
    }

    /* 包号不连续：回路最后收下的包号，让主机从那里重发 */
    if ((s_iap.have_last != 0U) && (id != (uint16_t)(s_iap.last_packet_id + 1U))) {
        out[0] = (uint8_t)(s_iap.last_packet_id >> 8);
        out[1] = (uint8_t)(s_iap.last_packet_id & 0xFFU);
        e = send_ack(0x06, out, 2U);
        return (e != ERR_OK) ? e : ERR_IAP_SEQ;
    }

    if ((s_iap.prog_bytes + payload_len) > s_iap.file_size) {
        return ERR_IAP_SIZE;
    }

    fmc_unlock();
    fmc_clear_flags();
    flash_write(&frame[FRAME_HDR_LEN + 2U], payload_len);
    s_iap.prog_bytes += payload_len;
    if (s_iap.prog_bytes == s_iap.file_size) {
        flash_flush_word();
    }
    fmc_lock();

    s_iap.recv_packets++;
    s_iap.last_packet_id = id;
    s_iap.have_last = 1U;

    out[0] = frame[5];
    out[1] = frame[6];
    e = send_ack(0x06, out, 2U);
    if (e != ERR_OK) {
        return e;
    }
    return (s_iap.error != 0U) ? ERR_IAP_FLASH : ERR_OK;
}

/* 0x06 总数据包数：记录主机包数，回实际收到的包数 */
parse_err_t ack_number_of_packets(const uint8_t *frame, uint16_t len)
{
    uint8_t data_len = (uint8_t)(frame[2] - FRAME_HDR_LEN);
    uint8_t out[2];
    parse_err_t e;

    (void)len;

    if (s_iap.active == 0U) {
        return ERR_IAP_STATE;
    }
    if (data_len < 2U) {
        return ERR_IAP_PARAM;
    }

    s_iap.declared_packets = (uint16_t)(((uint16_t)frame[5] << 8) | frame[6]);

    out[0] = (uint8_t)(s_iap.recv_packets >> 8);
    out[1] = (uint8_t)(s_iap.recv_packets & 0xFFU);
    e = send_ack(0x07, out, 2U);
    if (e != ERR_OK) {
        return e;
    }
    return (s_iap.recv_packets == s_iap.declared_packets) ? ERR_OK : ERR_IAP_CRC;
}

/* 0x07 总数据校验：对写入 flash 的整个文件算 CRC16，回 0xF2 0x08 */
parse_err_t ack_all_data_crc(const uint8_t *frame, uint16_t len)
{
    uint8_t data_len = (uint8_t)(frame[2] - FRAME_HDR_LEN);
    uint16_t expected;
    uint16_t calc;
    uint8_t out[2];
    parse_err_t e;

    (void)len;

    if (s_iap.active == 0U) {
        return ERR_IAP_STATE;
    }
    if (s_iap.size_ok == 0U) {
        return ERR_IAP_SIZE;
    }
    if (data_len < 2U) {
        return ERR_IAP_PARAM;
    }

    expected = (uint16_t)(((uint16_t)frame[5] << 8) | frame[6]);

    fmc_unlock();
    fmc_clear_flags();
    flash_flush_word();
    fmc_lock();

    calc = crc16_flash(s_iap.base_addr, s_iap.file_size);
    s_iap.crc_ok = ((calc == expected) &&
                    (s_iap.prog_bytes == s_iap.file_size) &&
                    (s_iap.error == 0U)) ? 1U : 0U;

    out[0] = (uint8_t)(calc >> 8);
    out[1] = (uint8_t)(calc & 0xFFU);
    e = send_ack(0x08, out, 2U);
    if (e != ERR_OK) {
        return e;
    }
    if (s_iap.error != 0U) {
        return ERR_IAP_FLASH;
    }
    return (s_iap.crc_ok != 0U) ? ERR_OK : ERR_IAP_CRC;
}

/* 0x08 完成重启：校验通过后回 0xF2 0x09 并复位 */
parse_err_t ready_rebot(const uint8_t *frame, uint16_t len)
{
    parse_err_t e;

    (void)frame;
    (void)len;

    if ((s_iap.active == 0U) || (s_iap.erased == 0U)) {
        return ERR_IAP_STATE;
    }
    if ((s_iap.file_size == 0U) || (s_iap.prog_bytes != s_iap.file_size)) {
        return ERR_IAP_PARAM;
    }
    if (s_iap.error != 0U) {
        return ERR_IAP_FLASH;
    }
    if (s_iap.crc_ok == 0U) {
        return ERR_IAP_CRC;
    }
    if ((s_iap.declared_packets != 0U) &&
        (s_iap.recv_packets != s_iap.declared_packets)) {
        return ERR_IAP_CRC;
    }

    e = send_ack(0x09, NULL, 0U);
    if (e != ERR_OK) {
        return e;
    }

    vTaskDelay(pdMS_TO_TICKS(20));
    NVIC_SystemReset();
    return ERR_OK;
}
