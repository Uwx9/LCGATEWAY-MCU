#include "FreeRTOS.h"
#include "gd32f3x0.h"
#include "parse.h"
#include "parse-error.h"
#include "operats.h"
#include "task.h"
#include <stdint.h>

_Static_assert(FRAME_LEN_MAX <= RXBUF_SIZE, "frame larger than RX ring");

typedef enum {
    S_HEAD = 0, S_LEN, S_CMD, S_DATA, S_CRC_L, S_CRC_H
} parse_state_t;

static struct {
    parse_state_t st;
    uint16_t idx;
    uint8_t  len;      /* LEN 字段：整帧字节数 */
    uint8_t  need;     /* 还差多少个数据字节 */
    uint8_t  done;     /* 本次突发已经交付一帧 */
    uint8_t  buf[RXBUF_SIZE];  /* 整帧：HEAD..DATA + CRC */
} p;

/* 环的读指针：已经消费掉的绝对字节数，只在解析任务里修改 */
static uint32_t read_total;
extern volatile uint32_t read_total_wraps;
extern volatile uint32_t dma_total_wraps;
extern volatile uint32_t rx_dma_wraps;
extern volatile uint32_t rx_dma_wraps_wraps;

#ifdef TEST5
/* 预置命令会主动跳变计数，本次突发的 tail 检查需要跳过 */
static uint8_t s_test_jump;
#endif

/* 虽然正常情况avail最大为FRAME_MAX_SIZE, 但这里允许主机传RXBUF_SIZE大小字节 */
extern uint8_t rxbuffer[RXBUF_SIZE];

/* 只接受主机的 IAP 指令和测试指令 */
static uint8_t is_host_cmd(uint8_t cmd)
{
    if ((cmd >= IAP_CMD_SET_DO) && (cmd <= IAP_CMD_FINISH)) {
        return 1U;
    }
#ifdef TEST5
    if (cmd == TEST_CMD) {
        return 1U;
    }
#endif
    return 0U;
}

/* 状态机复位 */
static void parse_reset(void)
{
    p.st   = S_HEAD;
    p.idx  = 0;
    p.len  = 0;
    p.need = 0;
    p.done = 0;
}

/* 初始化解析器，必须在启动 DMA 接收之前调用 */
void parse_init(void)
{
    parse_reset();
    read_total = 0;
}

uint32_t parse_read_total(void)
{
    return read_total;
}

#ifdef TEST5
/*
 * 测试用：把 rx_dma_wraps 和 read_total 一起挪到 lap*RXBUF_SIZE+pos。
 * pos 取 DMA 当前写位置，保证预置后 avail 为 0。
 */
uint32_t parse_test_preset(uint32_t lap)
{
    uint32_t cnt;
    uint32_t pos;
    uint32_t total;

    taskENTER_CRITICAL();
    cnt   = dma_transfer_number_get(DMA_CH2);
    pos   = (RXBUF_SIZE - cnt) % RXBUF_SIZE;
    total = lap * RXBUF_SIZE + pos;
    rx_dma_wraps = lap;
    read_total   = total;
    /* 让三个回绕计数与 lap*RXBUF_SIZE 这个总字节数保持一致 */
    dma_total_wraps    = lap >> 24;
    read_total_wraps   = lap >> 24;
    rx_dma_wraps_wraps = 0U;
    s_test_jump        = 1U;
    taskEXIT_CRITICAL();

    return total;
}

void parse_test_counters(uint32_t out[5])
{
    taskENTER_CRITICAL();
    out[0] = rx_dma_wraps;
    out[1] = read_total;
    out[2] = dma_total_wraps;
    out[3] = read_total_wraps;
    out[4] = rx_dma_wraps_wraps;
    taskEXIT_CRITICAL();
}
#endif

/**
 * @brief CRC16校验 *
 * @details 用于Modbus通信的CRC16校验 *
 * @param cpucData 数据缓冲区 *
 * @param len 数据长度 *
 * @return uint16_t 返回值 *
 * @since V1.0.0
 */
uint16_t CrcValueCalc(const uint8_t *cpucData, uint32_t len)
{
    uint16_t usCrcValue = 0xFFFF;
    int iI = 0;
    while (len--) {
        usCrcValue ^= *cpucData++;
        for (iI = 8 - 1; iI >= 0; iI--) {
            if (usCrcValue & 0x0001) {
                usCrcValue = (usCrcValue >> 1) ^ 0xA001;
            } else {
                usCrcValue = usCrcValue >> 1;
            }
        }
    }
    return (usCrcValue);
}

/* 解析一个字节并更新解析器；这一帧有问题就由调用方整帧丢弃 */
static parse_err_t parse_feed(const uint8_t b)
{
#ifdef DEBUG
    configASSERT(p.idx < RXBUF_SIZE);
#endif
    switch (p.st) {
    case S_HEAD:
        if (b != FRAME_HEAD) {
            return ERR_NO_HEAD;
        }
        p.buf[0] = b;
        p.idx = 1;
        p.st  = S_LEN;
        return ERR_OK;

    case S_LEN:
        if ((b < FRAME_LEN_MIN) || (b > FRAME_LEN_MAX)) {
            return ERR_BAD_LEN;
        }
        p.buf[p.idx++] = b;
        p.len  = b;
        p.need = (uint8_t)(b - FRAME_OVERHEAD);
        p.st   = S_CMD;
        return ERR_OK;

    case S_CMD:
        if (is_host_cmd(b) == 0U) {
            return ERR_BAD_CMD;
        }
        p.buf[p.idx++] = b;
        p.st = p.need ? S_DATA : S_CRC_L;
        return ERR_OK;

    case S_DATA:
        p.buf[p.idx++] = b;
        if (--p.need == 0U) {
            p.st = S_CRC_L;
        }
        return ERR_OK;

    case S_CRC_L:
        p.buf[p.idx++] = b;
        p.st = S_CRC_H;
        return ERR_OK;

    case S_CRC_H: {
        uint16_t got;
        uint16_t calc;

        p.buf[p.idx++] = b;
        got  = (uint16_t)p.buf[p.len - 2U]
               | ((uint16_t)p.buf[p.len - 1U] << 8);
        calc = CrcValueCalc(p.buf, (uint32_t)(p.len - 2U));

        if (calc == got) {
            p.done = 1;
            return ERR_OK;
        }
        return ERR_CRC;
    }

    default:
        return ERR_OK;
    }
}

/*
 * 一次 IDLE 突发最多处理一帧：
 * 好帧交付后、坏帧出错后，都把本次突发剩余字节一起丢掉。
 */
parse_err_t process_all_available(void)
{
    uint32_t tail;
    uint32_t avail;
    uint16_t pos;
    parse_err_t e = ERR_OK;

    /* 每次突发都从干净状态开始，上一次遗留的半帧在这里丢弃 */
    parse_reset();

    tail  = rx_dma_total();                    /* 写指针快照 */
    avail = tail - read_total;                 /* 无符号减法，回绕自动成立 */
    /* 准备消费的字节的位置 */
    pos   = (uint16_t)(read_total % RXBUF_SIZE); /* RXBUF_SIZE 是2的n次方就不会在readtotal溢出时破坏pos */

    if (avail == 0U) {
        goto out;
    }

    /* 落后超过一整圈：最旧的未读字节已经被 DMA 覆盖 */
    if (avail > RXBUF_SIZE) {
        e = ERR_OVERRUN;
        goto out;
    }

    while (read_total != tail) {
        e = parse_feed(rxbuffer[pos]);

        /* 这一帧有问题：整帧丢弃，等主机重发 */
        if (e != ERR_OK) {
            goto out;
        }
        if (++pos == RXBUF_SIZE)
            pos = 0;
        /* 每消费一个字节只在这里推进 */
        read_total++;
        if (read_total == 0U) {
            read_total_wraps++;
        }

        /* 一帧成功交付，本次突发后面的字节当噪声丢掉 */
        if (p.done) {
#ifdef DEBUG
            configASSERT(p.len <= FRAME_MAX_SIZE);
#endif
            e = on_frame(p.buf, p.len);
            parse_reset();

            /* 动作错误就直接out */
            if (e != ERR_OK) {
#ifdef TEST5
                s_test_jump = 0U;
#endif
                goto out;
            }

            /* 动作成功就break执行后续两个检查再out */
            break;
        }
    }

#ifdef TEST5
    if (s_test_jump != 0U) {
        s_test_jump = 0U;
        goto out;
    }
#endif

#ifdef DEBUG
    configASSERT(rx_dma_total() == tail);
#endif
    /* 解析期间 DMA 又前进了 */
    if (rx_dma_total() != tail) {
        e = ERR_RX_MOVED;
        goto out;
    }

    /*
     * IDLE 已经把这次突发截断，状态机却还停在帧中间，
     * 说明这一帧不完整，丢弃半帧；下次调用开头会 reset。
     */
    if (p.st != S_HEAD) {
        e = ERR_PARTIAL;
        goto out;
    }

    /* label: 后面必须跟一条语句，而声明不是语句 */
out: {
        uint32_t new_total = rx_dma_total();

        if (new_total < read_total) {
            read_total_wraps++;
        }
        read_total = new_total;
    }
    return e;
}

parse_err_t on_frame(const uint8_t *frame, uint16_t len)
{
    parse_err_t e = ERR_OK;
    uint8_t cmd = frame[2];

#ifdef DEBUG
    configASSERT(frame[0] == FRAME_HEAD);
    configASSERT(is_host_cmd(cmd) != 0U);
#endif
    switch (cmd) {
    case IAP_CMD_SET_DO: /* 设置DO */
        e = ack_set_DO(frame, len);
        goto out;
    case IAP_CMD_READ_DI: /* 读取DI */
        e = ack_read_DI(frame, len);
        goto out;
    case IAP_CMD_START: /* 启动升级，数据带待升级版本号 */
        e = ack_upgrade(frame, len);
        goto out;
    case IAP_CMD_FILE_SIZE: /* 发送文件大小 */
        e = ack_file_size(frame, len);
        goto out;
    case IAP_CMD_ERASE: /* 开始擦除 */
        e = ack_eraseing(frame, len);
        goto out;
    case IAP_CMD_DATA: /* 发送数据 */
        e = ack_packet_id(frame, len);
        goto out;
    case IAP_CMD_PACKETS: /* 总数据包数 */
        e = ack_number_of_packets(frame, len);
        goto out;
    case IAP_CMD_TOTAL_CRC: /* 总数据校验 */
        e = ack_all_data_crc(frame, len);
        goto out;
    case IAP_CMD_FINISH: /* 完成重启 */
        e = ready_rebot(frame, len);
        goto out;
#ifdef TEST5
    case TEST_CMD: /* 测试命令：预置/回读回绕计数 */
        e = test_cmd(frame, len);
        goto out;
#endif
    default:
        goto out;
    }

out:
    return e;
}
