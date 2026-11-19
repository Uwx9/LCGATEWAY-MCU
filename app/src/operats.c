#include "FreeRTOS.h"
#include "parse-error.h"
#include "task.h"

#include <stdint.h>
#include <string.h>

#include "gd32f3x0.h"

#include "DIDO.h"
#include "layout.h"
#include "memmap.h"
#include "operats.h"
#include "parse.h"
#include "version.h"

#define USART0_TDATA_ADDRESS   ((uint32_t)&USART_TDATA(USART0))

#define META_CRC_LEN           28U
#define DATA_PACKET_MAX        128U
#define ERASE_ACK_PAGES        8U      /* 擦除时按页数近似 500ms 发一次“擦除中” */
#define TX_TIMEOUT_MS          100U

/* DMA TX 完成中断置位 */
__IO FlagStatus g_transfer_complete = RESET;

/**
 * @brief IAP升级上下文控制块
 * @note 名字iap_ctx_t：
 *       IAP = In-Application Programming，在应用编程（MCU不依赖Loader，APP运行时自己烧Flash升级）
 *       ctx = context，上下文；
 *       这个结构体保存一次升级会话的全部状态、统计、缓存信息，
 *       全程只需要一个实例，贯穿【开始升级 → 收包 → 烧写 → 校验 → 结束】整个生命周期
 */
typedef struct {
    uint8_t  active;			// 升级会话是否激活：1=正在进行升级，0=空闲/无升级任务
    uint8_t  size_ok;			// 固件长度校验标记：1=文件大小合法且不超过 stage(50K)；0=未校验/超限
    uint8_t  erased;			// 升级分区擦除标记：1=目标Flash分区已经完成擦除；0=未擦除
    uint8_t  crc_ok;			// CRC校验结果标记：1=整包固件CRC校验通过；0=校验失败/还没校验
    uint8_t  flash_error;		// Flash 擦除或编程失败标记：1=本次升级中发生过 flash 操作错误；0=正常
    uint8_t  have_last;			// 是否已有上一个有效包号：1=last_packet_id 有效；0=还没收到过数据包
    uint8_t  word_len;			// 残余缓存内有效字节数：范围0~3，记录暂存的零散字节数量，用来拼成32bit字给flash_flush_word
    uint8_t new_version_major;	// 待升级固件的版本号major，主机下发固件时携带
    uint8_t new_version_minor;  // 待升级固件的版本号minor，主机下发固件时携带
    uint16_t declared_packets;	// 主机声明的总包数：数据包发完后由 0x07 命令告诉 MCU 一共发了多少包
    uint16_t recv_packets;		// MCU已经成功接收的数据包计数，用来和declared_packets对比判断是否收完
    uint16_t last_packet_id;	// 上一个收到的包ID，用来做丢包检测、防重复包、顺序校验
    uint16_t crc;				// 0x08 计算出的整包 CRC，0x09 写 metadata 用
    uint32_t file_size;			// 固件总字节长度（主机下发）
    uint32_t base_addr;			// 升级分区起始基地址
    uint32_t write_addr;		// 当前正在写入Flash的目标地址，每写一个word自动递增4
    uint32_t prog_bytes;		// 已经成功烧录到Flash的总字节数
    uint32_t word;				// 残余字节缓存：攒够4字节由 flash_write 写入；收尾不足4字节由 flash_flush_word 补0xFF后写入
} iap_ctx_t;

static iap_ctx_t s_iap;
static uint8_t s_tx[32];

/* 升级镜像固定写在 stage 区，由 bootloader 拷贝到 app 区 */
static uint32_t upgrade_base_addr(void)
{
    return STAGE_BASE;
}

static uint32_t free_upgrade_flash_size(void)
{
    return STAGE_SIZE;
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

/* 组应答帧：LEN 为整帧字节数（含 CRC），CRC 覆盖 LEN-2 个字节 */
static parse_err_t send_ack(uint8_t cmd, const uint8_t *data, uint8_t data_len)
{
    uint8_t len = (uint8_t)(FRAME_OVERHEAD + data_len);
    uint16_t crc;

    s_tx[0] = FRAME_HEAD;
    s_tx[1] = len;
    s_tx[2] = cmd;
    if ((data != NULL) && (data_len != 0U)) {
        memcpy(&s_tx[FRAME_DATA_OFF], data, data_len);
    }

    crc = CrcValueCalc(s_tx, (uint32_t)(len - 2U));
    s_tx[len - 2U] = (uint8_t)(crc & 0xFFU);
    s_tx[len - 1U] = (uint8_t)(crc >> 8);

    return tx_dma(s_tx, len);
}

static void fmc_clear_flags(void)
{
    fmc_flag_clear(FMC_FLAG_END | FMC_FLAG_PGERR | FMC_FLAG_WPERR);
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
        s_iap.flash_error = 1U;
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
                s_iap.flash_error = 1U;
            }
            s_iap.write_addr += 4U;
            s_iap.word = 0U;
            s_iap.word_len = 0U;
        }
    }
}

/*
 * 0x01 设置DO, 数据分别为DO编号和状态(0表示拉低，1表示拉高)
 * 返回数据表示设置DO是否成功(0表示成功，非0表示失败)
 */
parse_err_t ack_set_DO(const uint8_t *frame, uint16_t len)
{
    uint8_t data_len = (uint8_t)(len - FRAME_OVERHEAD);
    uint8_t out;
    parse_err_t e;

    if (data_len != 2U) {
        return ERR_IAP_PARAM;
    }

    out = (uint8_t)DO_set(frame[FRAME_DATA_OFF], frame[FRAME_DATA_OFF + 1U]);

    e = send_ack(IAP_ACK(IAP_CMD_SET_DO), &out, 1U);
    if (e != ERR_OK) {
        return e;
    }
    return (out == 0U) ? ERR_OK : ERR_IAP_SET_DO;
}

/*
 * 0x02 读取DI：DI 的 GPIO 映射确定后再实现。
 * 目前直接返回错误，主机侧会超时。
 */
parse_err_t ack_read_DI(const uint8_t *frame, uint16_t len)
{
    (void)frame;
    (void)len;

    return ERR_IAP_READ_DI;
}

/*
 * 0x03 启动升级：数据为待升级版本号（major, minor），
 * 只有版本高于当前固件才允许升级，应答 0x83 + 1 字节（1=可以）。
 */
parse_err_t ack_upgrade(const uint8_t *frame, uint16_t len)
{
    uint8_t data_len = (uint8_t)(len - FRAME_OVERHEAD);
    uint8_t new_major;
    uint8_t new_minor;
    uint8_t can_upgrade;
    uint8_t out;
    parse_err_t e;

    if (data_len != 2U) {
        return ERR_IAP_PARAM;
    }

    new_major = frame[FRAME_DATA_OFF];
    new_minor = frame[FRAME_DATA_OFF + 1U];

    can_upgrade = ((new_major > g_fw_version_num.major) ||
                   ((new_major == g_fw_version_num.major) &&
                    (new_minor > g_fw_version_num.minor))) ? 1U : 0U;

	//TODO 
	// ack_upgrade() 无条件 memset，重复/过期 0x03 会破坏正在进行的会话
	//    src/operats.c:240 不论 can_upgrade 是否为 1 都清零 s_iap；
	//    如果主机因 ACK 丢失重发 0x03，或误发旧版本 0x03，write_addr 回到 base、have_last=0，
	// 下一次 0x06 会被当成首包从 base 开始写，镜像整体错位，最后 CRC 失败。
	//    建议：active 时拒绝新的 0x03，或让 0x03 幂等（不重置已有进度）。
    memset(&s_iap, 0, sizeof(s_iap));
    if (can_upgrade != 0U) {
        s_iap.active             = 1U;
        s_iap.base_addr          = upgrade_base_addr();
        s_iap.write_addr         = s_iap.base_addr;
        s_iap.new_version_major  = new_major;
        s_iap.new_version_minor  = new_minor;
    }

    out = can_upgrade;
    e = send_ack(IAP_ACK(IAP_CMD_START), &out, 1U);
    if (e != ERR_OK) {
        return e;
    }
    return (can_upgrade != 0U) ? ERR_OK : ERR_IAP_VERSION;
}

/*
 * 0x04 发送文件大小：检查剩余 flash 是否放得下，回 0x84 + 文件大小
 * 这里有错就不send_ack了直接返回错误
 */
parse_err_t ack_file_size(const uint8_t *frame, uint16_t len)
{
    uint8_t data_len = (uint8_t)(len - FRAME_OVERHEAD);
    uint32_t size;
    uint8_t out[4];
    parse_err_t e;

    if (s_iap.active == 0U) {
        return ERR_IAP_STATE;
    }
    if (data_len != 4U) {
        return ERR_IAP_PARAM;
    }

    size = ((uint32_t)frame[FRAME_DATA_OFF] << 24) |
           ((uint32_t)frame[FRAME_DATA_OFF + 1U] << 16) |
           ((uint32_t)frame[FRAME_DATA_OFF + 2U] << 8) |
           (uint32_t)frame[FRAME_DATA_OFF + 3U];

    s_iap.file_size = size;
    /* size != 0 && size <= stage(50K) */
    s_iap.size_ok = ((size != 0U) && (size <= free_upgrade_flash_size())) ? 1U : 0U;

    /* 从高位开始发 */
    if (s_iap.size_ok != 0U) {
        out[0] = (uint8_t)(size >> 24);
        out[1] = (uint8_t)(size >> 16);
        out[2] = (uint8_t)(size >> 8);
        out[3] = (uint8_t)size;
        e = send_ack(IAP_ACK(IAP_CMD_FILE_SIZE), out, 4U);
        if (e != ERR_OK)
            return e;
    }
    /* size不对, 返回ERR_IAP_SIZE */
    return (s_iap.size_ok != 0U) ? ERR_OK : ERR_IAP_SIZE;
}

/* 0x05 开始擦除：擦除升级暂存区，期间回 0x85 + 擦除进度（0~100） */
parse_err_t ack_eraseing(const uint8_t *frame, uint16_t len)
{
    uint32_t addr;
    uint32_t pages_done = 0U;
    uint32_t total_pages;
    uint8_t progress = 0U;
    parse_err_t e;

    (void)frame;
    (void)len;

    if (s_iap.active == 0U) {
        return ERR_IAP_STATE;
    }
    if (s_iap.size_ok == 0U) {
        return ERR_IAP_SIZE;
    }

    total_pages = (STAGE_END - s_iap.base_addr) / FLASH_PAGE_SIZE;
    if (total_pages == 0U) {
        return ERR_IAP_SIZE;
    }

    /* 先回 0% 告诉主机擦除已经开始 */
    e = send_ack(IAP_ACK(IAP_CMD_ERASE), &progress, 1U);
    if (e != ERR_OK) {
        return e;
    }

    for (addr = s_iap.base_addr; addr < STAGE_END; addr += FLASH_PAGE_SIZE) {
        fmc_unlock();
        fmc_clear_flags();
        if (fmc_page_erase(addr) != FMC_READY) {
            fmc_lock();
            s_iap.flash_error = 1U;
            return ERR_IAP_FLASH;
        }
        fmc_lock();

        pages_done++;
        progress = (uint8_t)((pages_done * 100U) / total_pages);

        /* 每擦除 8 页回一次进度，最后一页回 100 */
        if (((pages_done % ERASE_ACK_PAGES) == 0U) ||
            (pages_done == total_pages)) {
            e = send_ack(IAP_ACK(IAP_CMD_ERASE), &progress, 1U);
            if (e != ERR_OK) {
                return e;
            }
        }
    }

    /* 擦除完成 */
    s_iap.erased       = 1U;
    s_iap.write_addr   = s_iap.base_addr;
    s_iap.prog_bytes   = 0U;
    s_iap.recv_packets = 0U;
    s_iap.have_last    = 0U;
    s_iap.word         = 0U;
    s_iap.word_len     = 0U;
	s_iap.crc_ok	   = 0U;
	s_iap.crc		   = 0U;
	s_iap.flash_error  = 0U;

    return ERR_OK;
}

/* 0x06 发送数据：包号(2 字节, 高在前) + 最多 128 字节数据，回 0x86 + 包号 */
parse_err_t ack_packet_id(const uint8_t *frame, uint16_t len)
{
    uint16_t data_len = (uint16_t)(len - FRAME_OVERHEAD);
    uint16_t id;
    uint16_t payload_len;
    uint8_t out[2];
    parse_err_t e;

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

    id = (uint16_t)(((uint16_t)frame[FRAME_DATA_OFF] << 8) |
                    frame[FRAME_DATA_OFF + 1U]);
	if (s_iap.have_last == 0 && id != 0)
		return ERR_IAP_SEQ;

    /* 重发的包只回 ACK，不重复写 flash */
    if ((s_iap.have_last != 0U) && (id == s_iap.last_packet_id)) {
        out[0] = frame[FRAME_DATA_OFF];
        out[1] = frame[FRAME_DATA_OFF + 1U];
        return send_ack(IAP_ACK(IAP_CMD_DATA), out, 2U);
    }

    /* 包号不连续：回路最后收下的包号，让主机从那里重发 */
    if ((s_iap.have_last != 0U) && (id != (uint16_t)(s_iap.last_packet_id + 1U))) {
        out[0] = (uint8_t)(s_iap.last_packet_id >> 8);
        out[1] = (uint8_t)(s_iap.last_packet_id & 0xFFU);
        e = send_ack(IAP_ACK(IAP_CMD_DATA), out, 2U);
        return (e != ERR_OK) ? e : ERR_IAP_SEQ;
    }

    if ((s_iap.prog_bytes + payload_len) > s_iap.file_size) {
        return ERR_IAP_SIZE;
    }

    /* 新数据到来，之前的整包 CRC 失效 */
    s_iap.crc_ok = 0U;
    s_iap.crc = 0U;

    /* 写flash */
    fmc_unlock();
    fmc_clear_flags();
    flash_write(&frame[FRAME_DATA_OFF + 2U], payload_len);
    s_iap.prog_bytes += payload_len;
    if (s_iap.prog_bytes == s_iap.file_size) {
        flash_flush_word();
    }
    fmc_lock();

    s_iap.recv_packets++;
    s_iap.last_packet_id = id;
    s_iap.have_last = 1U;

    out[0] = frame[FRAME_DATA_OFF];
    out[1] = frame[FRAME_DATA_OFF + 1U];
    e = send_ack(IAP_ACK(IAP_CMD_DATA), out, 2U);
    if (e != ERR_OK) {
        return e;
    }
    return (s_iap.flash_error != 0U) ? ERR_IAP_FLASH : ERR_OK;
}

/* 0x07 总数据包数：记录主机包数，回 0x87 + 实际收到的包数 */
parse_err_t ack_number_of_packets(const uint8_t *frame, uint16_t len)
{
    uint8_t data_len = (uint8_t)(len - FRAME_OVERHEAD);
    uint8_t out[2];
    parse_err_t e;

    if (s_iap.active == 0U) {
        return ERR_IAP_STATE;
    }
    if (data_len != 2U) {
        return ERR_IAP_PARAM;
    }

    s_iap.declared_packets = (uint16_t)(((uint16_t)frame[FRAME_DATA_OFF] << 8) |
                                        frame[FRAME_DATA_OFF + 1U]);

    out[0] = (uint8_t)(s_iap.recv_packets >> 8);
    out[1] = (uint8_t)(s_iap.recv_packets & 0xFFU);
    e = send_ack(IAP_ACK(IAP_CMD_PACKETS), out, 2U);
    if (e != ERR_OK) {
        return e;
    }
    return (s_iap.recv_packets == s_iap.declared_packets) ? ERR_OK : ERR_IAP_CRC;
}

/* 0x08 总数据校验：对写入 flash 的整个文件算 CRC16，回 0x88 + 计算值 */
parse_err_t ack_all_data_crc(const uint8_t *frame, uint16_t len)
{
    uint8_t data_len = (uint8_t)(len - FRAME_OVERHEAD);
    uint16_t expected;
    uint16_t calc;
    uint8_t out[2];
    parse_err_t e;

    if (s_iap.active == 0U) {
        return ERR_IAP_STATE;
    }
    if (s_iap.size_ok == 0U) {
        return ERR_IAP_SIZE;
    }
    if (data_len != 2U) {
        return ERR_IAP_PARAM;
    }

    expected = (uint16_t)(((uint16_t)frame[FRAME_DATA_OFF] << 8) |
                          frame[FRAME_DATA_OFF + 1U]);

    fmc_unlock();
    fmc_clear_flags();
    flash_flush_word();
    fmc_lock();

    calc = CrcValueCalc((const uint8_t *)s_iap.base_addr, s_iap.file_size);
    s_iap.crc = calc;
    s_iap.crc_ok = ((calc == expected) &&
                    (s_iap.prog_bytes == s_iap.file_size) &&
                    (s_iap.flash_error == 0U)) ? 1U : 0U;

    out[0] = (uint8_t)(calc >> 8);
    out[1] = (uint8_t)(calc & 0xFFU);
    e = send_ack(IAP_ACK(IAP_CMD_TOTAL_CRC), out, 2U);
    if (e != ERR_OK) {
        return e;
    }
    if (s_iap.flash_error != 0U) {
        return ERR_IAP_FLASH;
    }
    return (s_iap.crc_ok != 0U) ? ERR_OK : ERR_IAP_CRC;
}

static int app_meta_valid(const update_meta_t *m)
{
    if (m->magic != UPDATE_MAGIC) {
        return 0;
    }
    if ((m->size == 0U) || (m->size > APP_SIZE)) {
        return 0;
    }
    if ((m->state != UPDATE_STATE_REQUEST) &&
        (m->state != UPDATE_STATE_COPYING) &&
        (m->state != UPDATE_STATE_VALID) &&
        (m->state != UPDATE_STATE_FAILED)) {
        return 0;
    }

    return (CrcValueCalc((const uint8_t *)m, META_CRC_LEN) == m->meta_crc) ? 1 : 0;
}

static const update_meta_t *app_meta_active(void)
{
    const update_meta_t *a = (const update_meta_t *)META_A_ADDR;
    const update_meta_t *b = (const update_meta_t *)META_B_ADDR;
    int va = app_meta_valid(a);
    int vb = app_meta_valid(b);

    if (va && vb) {
        return (a->seq >= b->seq) ? a : b;
    }
    if (va) {
        return a;
    }
    if (vb) {
        return b;
    }

    return 0;
}

static int app_meta_write(uint32_t state, uint32_t size, uint32_t crc,
                          uint32_t prev_size, uint32_t prev_crc)
{
    const update_meta_t *active = app_meta_active();
    update_meta_t m;
    uint32_t seq = 1U;
    uint32_t addr = META_A_ADDR;
    uint32_t i;

    if (active != 0) {
        seq = active->seq + 1U;
        addr = (active == (const update_meta_t *)META_A_ADDR) ? META_B_ADDR : META_A_ADDR;
    }

    m.magic = UPDATE_MAGIC;
    m.seq = seq;
    m.state = state;
    m.size = size;
    m.crc = crc;
    m.prev_size = prev_size;
    m.prev_crc = prev_crc;
    m.meta_crc = CrcValueCalc((const uint8_t *)&m, META_CRC_LEN);

    fmc_unlock();
    fmc_clear_flags();
    if (fmc_page_erase(addr) != FMC_READY) {
        fmc_lock();
        return -1;
    }
    for (i = 0U; i < (sizeof(m) / sizeof(uint32_t)); i++) {
        if (fmc_word_program(addr + 4U * i, ((const uint32_t *)&m)[i]) != FMC_READY) {
            fmc_lock();
            return -1;
        }
    }
    fmc_lock();

    return 0;
}

/* 0x09 完成：校验通过后写 metadata REQUEST，回 0x89 并复位，由 bootloader 拷贝 */
parse_err_t ready_rebot(const uint8_t *frame, uint16_t len)
{
    parse_err_t e;
    uint32_t prev_size;
    uint32_t prev_crc;

    (void)frame;
    (void)len;

    if ((s_iap.active == 0U) || (s_iap.erased == 0U)) {
        return ERR_IAP_STATE;
    }
    if ((s_iap.file_size == 0U) || (s_iap.prog_bytes != s_iap.file_size)) {
        return ERR_IAP_PARAM;
    }
    if (s_iap.flash_error != 0U) {
        return ERR_IAP_FLASH;
    }
    if (s_iap.crc_ok == 0U) {
        return ERR_IAP_CRC;
    }
    if ((s_iap.declared_packets != 0U) &&
        (s_iap.recv_packets != s_iap.declared_packets)) {
        return ERR_IAP_CRC;
    }

    /* 记录当前正在运行的 app，stage 丢失时 boot 可以校验并回退 */
    prev_size = memmap_image_size();
    prev_crc = CrcValueCalc((const uint8_t *)memmap_flash_start(), prev_size);

    if (app_meta_write(UPDATE_STATE_REQUEST, s_iap.file_size, s_iap.crc,
                       prev_size, prev_crc) != 0) {
        return ERR_IAP_FLASH;
    }

    e = send_ack(IAP_ACK(IAP_CMD_FINISH), NULL, 0U);
    if (e != ERR_OK) {
        return e;
    }

    /* 等最后一帧发完，复位后由 bootloader 完成 stage -> app 拷贝 */
    vTaskDelay(pdMS_TO_TICKS(20));
    NVIC_SystemReset();

    return ERR_OK;
}

#ifdef TEST5
#define TEST_OP_PRESET  0x00U
#define TEST_OP_STATS   0x01U

/*
 * 0x7E 测试命令，data[0] 为操作码：
 *   0x00: data[1..4] 为大端 lap，按 lap*BUF_SIZE 预置全部计数
 *   0x01: 只回读
 * 两种操作都应答 5 个大端 u32:
 *   rx_dma_wraps, read_total, dma_total_wraps,
 *   read_total_wraps, rx_dma_wraps_wraps
 */
parse_err_t test_cmd(const uint8_t *frame, uint16_t len)
{
    uint16_t data_len = (uint16_t)(len - FRAME_OVERHEAD);
    uint8_t out[20];
    uint32_t v[5];
    uint32_t lap;
    unsigned i;

    if (data_len < 1U) {
        return ERR_IAP_PARAM;
    }

    switch (frame[FRAME_DATA_OFF]) {
    case TEST_OP_PRESET:
        if (data_len < 5U) {
            return ERR_IAP_PARAM;
        }
        lap = ((uint32_t)frame[FRAME_DATA_OFF + 1U] << 24) |
              ((uint32_t)frame[FRAME_DATA_OFF + 2U] << 16) |
              ((uint32_t)frame[FRAME_DATA_OFF + 3U] << 8) |
              (uint32_t)frame[FRAME_DATA_OFF + 4U];
        (void)parse_test_preset(lap);
        break;

    case TEST_OP_STATS:
        break;

    default:
        return ERR_IAP_PARAM;
    }

    parse_test_counters(v);
    for (i = 0U; i < 5U; i++) {
        out[4U * i]      = (uint8_t)(v[i] >> 24);
        out[4U * i + 1U] = (uint8_t)(v[i] >> 16);
        out[4U * i + 2U] = (uint8_t)(v[i] >> 8);
        out[4U * i + 3U] = (uint8_t)v[i];
    }
    return send_ack(IAP_ACK(TEST_CMD), out, 20U);
}
#endif
