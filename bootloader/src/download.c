#include <stdint.h>

#include "gd32f3x0.h"
#include "crc16.h"
#include "layout.h"
#include "boot_flash.h"
#include "download.h"

/*
 * 串口下载模式：只负责把主机发来的镜像写进 stage，收完后写
 * metadata REQUEST 并复位，后续拷贝/校验全部交给 boot 现有状态机。
 *
 * 帧格式与 app 侧一致：
 *   [0] HEAD 0x40 | [1] LEN（整帧） | [2] CMD | [3..] DATA | CRC16(低字节在前)
 * 只处理升级相关命令 0x03~0x09；0x01/0x02 属于运行期功能，不在 boot 里实现。
 */

#define DL_FRAME_HEAD       0x40U
#define DL_FRAME_DATA_MAX   130U    /* 2 字节包号 + 最多 128 字节数据 */
#define DL_FRAME_OVERHEAD   5U
#define DL_FRAME_MAX        (DL_FRAME_OVERHEAD + DL_FRAME_DATA_MAX)
#define DL_PACKET_MAX       128U
#define DL_ACK(cmd)         ((uint8_t)((cmd) | 0x80U))

#define DL_CMD_START        0x03U
#define DL_CMD_FILE_SIZE    0x04U
#define DL_CMD_ERASE        0x05U
#define DL_CMD_DATA         0x06U
#define DL_CMD_PACKETS      0x07U
#define DL_CMD_TOTAL_CRC    0x08U
#define DL_CMD_FINISH       0x09U
#define DL_CMD_STATUS       0x0AU

#define DL_WAIT_MS          2000U   /* 等下一帧 HEAD */
#define DL_BYTE_MS          100U    /* 帧内字节间隔超时 */
#define DL_ERASE_ACK_PAGES  8U

typedef struct {
    uint8_t  active;
    uint8_t  size_ok;
    uint8_t  erased;
    uint8_t  crc_ok;
    uint8_t  flash_error;
    uint8_t  have_last;
    uint8_t  word_len;
    uint16_t declared_packets;
    uint16_t recv_packets;
    uint16_t last_packet_id;
    uint16_t crc;
    uint32_t file_size;
    uint32_t write_addr;
    uint32_t prog_bytes;
    uint32_t word;
} dl_ctx_t;

static dl_ctx_t s_dl;
static uint8_t s_frame[DL_FRAME_MAX];
static uint8_t s_tx[16];
static uint32_t s_ms;

static void dl_ctx_clear(void)
{
    uint8_t *p = (uint8_t *)&s_dl;
    uint32_t i;

    for (i = 0U; i < sizeof(s_dl); i++) {
        p[i] = 0U;
    }
}

static void dl_tick_init(void)
{
    SysTick->LOAD = (SystemCoreClock / 1000U) - 1U;
    SysTick->VAL = 0U;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_ENABLE_Msk;
}

static uint32_t dl_millis(void)
{
    if ((SysTick->CTRL & SysTick_CTRL_COUNTFLAG_Msk) != 0U) {
        s_ms++;
    }
    return s_ms;
}

static void dl_uart_init(void)
{
    rcu_periph_clock_enable(RCU_GPIOA);
    rcu_periph_clock_enable(RCU_USART0);

    gpio_af_set(GPIOA, GPIO_AF_1, GPIO_PIN_9);
    gpio_af_set(GPIOA, GPIO_AF_1, GPIO_PIN_10);
    gpio_mode_set(GPIOA, GPIO_MODE_AF, GPIO_PUPD_PULLUP, GPIO_PIN_9);
    gpio_mode_set(GPIOA, GPIO_MODE_AF, GPIO_PUPD_PULLUP, GPIO_PIN_10);
    gpio_output_options_set(GPIOA, GPIO_OTYPE_PP, GPIO_OSPEED_10MHZ, GPIO_PIN_9);

    usart_deinit(USART0);
    usart_word_length_set(USART0, USART_WL_8BIT);
    usart_stop_bit_set(USART0, USART_STB_1BIT);
    usart_parity_config(USART0, USART_PM_NONE);
    usart_baudrate_set(USART0, 115200U);
    usart_receive_config(USART0, USART_RECEIVE_ENABLE);
    usart_transmit_config(USART0, USART_TRANSMIT_ENABLE);
    usart_enable(USART0);
}

static int dl_getc(uint8_t *b, uint32_t timeout_ms)
{
    uint32_t start = dl_millis();

    while ((USART_STAT(USART0) & USART_STAT_RBNE) == 0U) {
        if ((dl_millis() - start) >= timeout_ms) {
            return 0;
        }
    }
    *b = (uint8_t)USART_RDATA(USART0);

    return 1;
}

static void dl_putc(uint8_t b)
{
    while ((USART_STAT(USART0) & USART_STAT_TBE) == 0U) {
    }
    USART_TDATA(USART0) = b;
}

static void dl_tx_wait(void)
{
    while ((USART_STAT(USART0) & USART_STAT_TC) == 0U) {
    }
}

/* 下载模式的人读提示，纯 ASCII，不参与帧协议 */
static void dl_puts(const char *s)
{
    while (*s != '\0') {
        dl_putc((uint8_t)*s++);
    }
    dl_tx_wait();
}

static void dl_ack(uint8_t cmd, const uint8_t *data, uint8_t data_len)
{
    uint8_t len = (uint8_t)(DL_FRAME_OVERHEAD + data_len);
    uint16_t crc;
    uint8_t i;

    s_tx[0] = DL_FRAME_HEAD;
    s_tx[1] = len;
    s_tx[2] = cmd;
    for (i = 0U; i < data_len; i++) {
        s_tx[3U + i] = data[i];
    }
    crc = crc16(s_tx, (uint32_t)(len - 2U));
    s_tx[len - 2U] = (uint8_t)(crc & 0xFFU);
    s_tx[len - 1U] = (uint8_t)(crc >> 8U);

    for (i = 0U; i < len; i++) {
        dl_putc(s_tx[i]);
    }
    dl_tx_wait();
}

/* 返回整帧长度；0 = 超时/没收到；-1 = 坏帧，调用方继续重同步 */
static int dl_read_frame(uint8_t *buf, uint32_t timeout_ms)
{
    uint8_t b;
    uint8_t len;
    uint8_t i;
    uint16_t got;
    uint32_t start;

    if (dl_getc(&b, timeout_ms) == 0) {
        return 0;
    }

    start = dl_millis();
    while (b != DL_FRAME_HEAD) {
        if ((dl_millis() - start) >= timeout_ms) {
            return 0;
        }
        if (dl_getc(&b, DL_BYTE_MS) == 0) {
            return 0;
        }
    }

    buf[0] = b;
    if (dl_getc(&buf[1], DL_BYTE_MS) == 0) {
        return -1;
    }
    len = buf[1];
    if ((len < DL_FRAME_OVERHEAD) || (len > DL_FRAME_MAX)) {
        return -1;
    }

    for (i = 2U; i < len; i++) {
        if (dl_getc(&buf[i], DL_BYTE_MS) == 0) {
            return -1;
        }
    }

    got = (uint16_t)buf[len - 2U] | ((uint16_t)buf[len - 1U] << 8U);
    if (crc16(buf, (uint32_t)(len - 2U)) != got) {
        return -1;
    }

    return (int)len;
}

static void dl_flash_write(const uint8_t *data, uint16_t len)
{
    uint16_t i;

    for (i = 0U; i < len; i++) {
        s_dl.word |= (uint32_t)data[i] << (8U * s_dl.word_len);
        s_dl.word_len++;
        if (s_dl.word_len == 4U) {
            if (boot_fmc_program_word(s_dl.write_addr, s_dl.word) != 0) {
                s_dl.flash_error = 1U;
            }
            s_dl.write_addr += 4U;
            s_dl.word = 0U;
            s_dl.word_len = 0U;
        }
    }
}

/* 把不足 4 字节的尾字补 0xFF 后写入 flash */
static void dl_flash_flush_word(void)
{
    uint32_t word;

    if (s_dl.word_len == 0U) {
        return;
    }
    word = s_dl.word;
    while (s_dl.word_len < 4U) {
        word |= 0xFFU << (8U * s_dl.word_len);
        s_dl.word_len++;
    }
    if (boot_fmc_program_word(s_dl.write_addr, word) != 0) {
        s_dl.flash_error = 1U;
    }
    s_dl.write_addr += 4U;
    s_dl.word = 0U;
    s_dl.word_len = 0U;
}

/* 0x0A 状态查询：app 不实现该命令，主机据此区分 boot 下载模式和 app */
static void dl_cmd_status(const uint8_t *f)
{
    uint8_t mode = 1U; /* 1 = bootloader 下载模式 */

    if (f[1] != DL_FRAME_OVERHEAD) {
        return;
    }
    dl_ack(DL_ACK(DL_CMD_STATUS), &mode, 1U);
}

/* 0x03 启动升级：boot 只做救砖下载，不比较版本，直接接受 */
static void dl_cmd_start(const uint8_t *f)
{
    uint8_t can_upgrade = 1U;

    if (f[1] != 7U) {
        return;
    }

    dl_ctx_clear();
    s_dl.active = 1U;
    dl_ack(DL_ACK(DL_CMD_START), &can_upgrade, 1U);
}

/* 0x04 文件大小：数据 4 字节大端，回显 4 字节 */
static void dl_cmd_file_size(const uint8_t *f)
{
    uint32_t size;
    uint8_t out[4];

    if ((s_dl.active == 0U) || (f[1] != 9U)) {
        return;
    }

    size = ((uint32_t)f[3] << 24) |
           ((uint32_t)f[4] << 16) |
           ((uint32_t)f[5] << 8) |
           (uint32_t)f[6];

    s_dl.file_size = size;
    s_dl.size_ok = ((size != 0U) && (size <= STAGE_SIZE)) ? 1U : 0U;

    if (s_dl.size_ok != 0U) {
        out[0] = (uint8_t)(size >> 24);
        out[1] = (uint8_t)(size >> 16);
        out[2] = (uint8_t)(size >> 8);
        out[3] = (uint8_t)size;
        dl_ack(DL_ACK(DL_CMD_FILE_SIZE), out, 4U);
    }
}

/* 0x05 擦除 stage，按页回显进度 */
static void dl_cmd_erase(const uint8_t *f)
{
    uint32_t addr;
    uint32_t pages_done = 0U;
    uint32_t total_pages = (STAGE_END - STAGE_BASE) / FLASH_PAGE_SIZE;
    uint8_t progress = 0U;

    if ((s_dl.active == 0U) || (s_dl.size_ok == 0U) ||
        (f[1] != 5U) || (total_pages == 0U)) {
        return;
    }

    dl_ack(DL_ACK(DL_CMD_ERASE), &progress, 1U);

    boot_fmc_unlock();
    for (addr = STAGE_BASE; addr < STAGE_END; addr += FLASH_PAGE_SIZE) {
        if (boot_fmc_erase_page(addr) != 0) {
            boot_fmc_lock();
            s_dl.flash_error = 1U;
            return;
        }

        pages_done++;
        progress = (uint8_t)((pages_done * 100U) / total_pages);
        if (((pages_done % DL_ERASE_ACK_PAGES) == 0U) ||
            (pages_done == total_pages)) {
            dl_ack(DL_ACK(DL_CMD_ERASE), &progress, 1U);
        }
    }
    boot_fmc_lock();

    s_dl.erased = 1U;
    s_dl.write_addr = STAGE_BASE;
    s_dl.prog_bytes = 0U;
    s_dl.recv_packets = 0U;
    s_dl.have_last = 0U;
    s_dl.word = 0U;
    s_dl.word_len = 0U;
    s_dl.crc_ok = 0U;
    s_dl.crc = 0U;
    s_dl.flash_error = 0U;
}

/* 0x06 数据包：2 字节包号 + 最多 128 字节数据，回显包号 */
static void dl_cmd_data(const uint8_t *f)
{
    uint16_t data_len = (uint16_t)(f[1] - DL_FRAME_OVERHEAD);
    uint16_t payload_len;
    uint16_t id;
    uint8_t out[2];

    if ((s_dl.active == 0U) || (s_dl.erased == 0U) ||
        (s_dl.size_ok == 0U) || (data_len < 2U)) {
        return;
    }

    payload_len = (uint16_t)(data_len - 2U);
    if (payload_len > DL_PACKET_MAX) {
        return;
    }

    id = (uint16_t)(((uint16_t)f[3] << 8) | f[4]);
    if ((s_dl.have_last == 0U) && (id != 0U)) {
        return;
    }

    /* 重发包只回 ACK，不重复写 flash */
    if ((s_dl.have_last != 0U) && (id == s_dl.last_packet_id)) {
        out[0] = f[3];
        out[1] = f[4];
        dl_ack(DL_ACK(DL_CMD_DATA), out, 2U);
        return;
    }

    /* 包号不连续：回最后收下的包号，让主机从那里重发 */
    if ((s_dl.have_last != 0U) && (id != (uint16_t)(s_dl.last_packet_id + 1U))) {
        out[0] = (uint8_t)(s_dl.last_packet_id >> 8);
        out[1] = (uint8_t)(s_dl.last_packet_id & 0xFFU);
        dl_ack(DL_ACK(DL_CMD_DATA), out, 2U);
        return;
    }

    if ((s_dl.prog_bytes + payload_len) > s_dl.file_size) {
        return;
    }

    s_dl.crc_ok = 0U;
    s_dl.crc = 0U;

    boot_fmc_unlock();
    dl_flash_write(&f[5], payload_len);
    s_dl.prog_bytes += payload_len;
    if (s_dl.prog_bytes == s_dl.file_size) {
        dl_flash_flush_word();
    }
    boot_fmc_lock();

    s_dl.recv_packets++;
    s_dl.last_packet_id = id;
    s_dl.have_last = 1U;

    out[0] = f[3];
    out[1] = f[4];
    dl_ack(DL_ACK(DL_CMD_DATA), out, 2U);
}

/* 0x07 总包数：记录声明值，回显实际收到值 */
static void dl_cmd_packets(const uint8_t *f)
{
    uint8_t out[2];

    if ((s_dl.active == 0U) || (f[1] != 7U)) {
        return;
    }

    s_dl.declared_packets = (uint16_t)(((uint16_t)f[3] << 8) | f[4]);
    out[0] = (uint8_t)(s_dl.recv_packets >> 8);
    out[1] = (uint8_t)(s_dl.recv_packets & 0xFFU);
    dl_ack(DL_ACK(DL_CMD_PACKETS), out, 2U);
}

/* 0x08 整包 CRC：回显 stage 中实际计算值 */
static void dl_cmd_total_crc(const uint8_t *f)
{
    uint16_t expected;
    uint16_t calc;
    uint8_t out[2];

    if ((s_dl.active == 0U) || (s_dl.size_ok == 0U) || (f[1] != 7U)) {
        return;
    }

    expected = (uint16_t)(((uint16_t)f[3] << 8) | f[4]);

    boot_fmc_unlock();
    dl_flash_flush_word();
    boot_fmc_lock();

    calc = crc16((const uint8_t *)STAGE_BASE, s_dl.file_size);
    s_dl.crc = calc;
    s_dl.crc_ok = ((calc == expected) &&
                   (s_dl.prog_bytes == s_dl.file_size) &&
                   (s_dl.flash_error == 0U)) ? 1U : 0U;

    out[0] = (uint8_t)(calc >> 8);
    out[1] = (uint8_t)(calc & 0xFFU);
    dl_ack(DL_ACK(DL_CMD_TOTAL_CRC), out, 2U);
}

/* stage 里的镜像至少要有可用的向量表，避免把坏镜像拷进 app */
static int dl_stage_vector_valid(void)
{
    uint32_t sp = *(const volatile uint32_t *)STAGE_BASE;
    uint32_t pc = *(const volatile uint32_t *)(STAGE_BASE + 4U);

    if ((sp < RAM_BASE) || (sp > RAM_END) || ((sp & 7U) != 0U)) {
        return 0;
    }
    if ((pc < APP_BASE) || (pc >= APP_END) || ((pc & 1U) == 0U)) {
        return 0;
    }

    return 1;
}

/* 0x09 完成：写 REQUEST 后复位，由 boot 现有流程完成 stage -> app */
static void dl_cmd_finish(const uint8_t *f)
{
    if ((s_dl.active == 0U) || (s_dl.erased == 0U) || (f[1] != 5U) ||
        (s_dl.size_ok == 0U) || (s_dl.crc_ok == 0U) ||
        (s_dl.prog_bytes != s_dl.file_size) ||
        (dl_stage_vector_valid() == 0)) {
        return;
    }
    if ((s_dl.declared_packets != 0U) &&
        (s_dl.declared_packets != s_dl.recv_packets)) {
        return;
    }

    if (boot_meta_write(UPDATE_STATE_REQUEST, s_dl.file_size, s_dl.crc) != 0) {
        return;
    }

    dl_ack(DL_ACK(DL_CMD_FINISH), 0, 0);
    NVIC_SystemReset();

    for (;;) {
    }
}

void download_mode(void)
{
    int len;

    boot_fmc_lock();
    dl_tick_init();
    dl_uart_init();
    dl_ctx_clear();
    dl_puts("\r\n[boot] download mode\r\n");

    for (;;) {
        len = dl_read_frame(s_frame, DL_WAIT_MS);
        if (len <= 0) {
            continue;
        }

        switch (s_frame[2]) {
        case DL_CMD_START:
            dl_cmd_start(s_frame);
            break;
        case DL_CMD_FILE_SIZE:
            dl_cmd_file_size(s_frame);
            break;
        case DL_CMD_ERASE:
            dl_cmd_erase(s_frame);
            break;
        case DL_CMD_DATA:
            dl_cmd_data(s_frame);
            break;
        case DL_CMD_PACKETS:
            dl_cmd_packets(s_frame);
            break;
        case DL_CMD_TOTAL_CRC:
            dl_cmd_total_crc(s_frame);
            break;
        case DL_CMD_FINISH:
            dl_cmd_finish(s_frame);
            break;
        case DL_CMD_STATUS:
            dl_cmd_status(s_frame);
            break;
        default:
            break;
        }
    }
}
