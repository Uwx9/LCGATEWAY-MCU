#include "FreeRTOS.h"
#include "parse.h"
#include "parse-error.h"
#include "task.h"
#include <stdint.h>

/* LEN 字段合法范围：至少要 5 字节头，整帧又不能FRAME */
#define FRAME_LEN_MIN		FRAME_HDR_LEN
#define FRAME_LEN_MAX		(FRAME_MAX_SIZE - 2)	 

typedef enum {
    S_HEAD = 0, S_RW, S_LEN, S_CMD, S_SUB, S_DATA, S_CRC_L, S_CRC_H
} parse_state_t;

static struct {
    parse_state_t st;
    uint16_t idx;
    uint8_t  len;      /* LEN 字段 */
    uint8_t  need;     /* 还差多少个数据字节 */
    uint8_t  done;     /* 本次突发已经交付一帧 */
    uint8_t  buf[RXBUF_SIZE];  /* 整帧：HEAD..DATA + CRC */
} p;

/* 环的读指针：已经消费掉的绝对字节数，只在解析任务里修改 */
static uint32_t read_total;

/* 由 main.c 提供的 DMA 环写指针（绝对字节数） */
extern uint32_t rx_dma_total(void);

/* 虽然正常情况avail最大为FRAME_MAX_SIZE, 但这里允许主机传RXBUF_SIZE大小字节 */
extern uint8_t rxbuffer[RXBUF_SIZE];

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

/**
 * @brief CRC16校验 *
 * @details 用于Modbus通信的CRC16校验 *
 * @param cpucData 数据缓冲区 *
 * @param usLength 数据长度 *
 * @return uint16_t 返回值 *
 * @since V1.0.0
 */
static uint16_t CrcValueCalc(const uint8_t *cpucData, uint16_t usLength)
{
    uint16_t usCrcValue = 0xFFFF;
    int iI = 0;
    while (usLength--) {
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
	configASSERT(p.idx < FRAME_MAX_SIZE);
#endif
    switch (p.st) {
    case S_HEAD:
        if (b != FRAME_HEAD) {
            return ERR_NO_HEAD;
        }
        p.buf[0] = b;
        p.idx = 1;
        p.st  = S_RW;
        return ERR_OK;

    case S_RW:
        if (b != FRAME_RW) {
            return ERR_BAD_RW;
        }
        p.buf[p.idx++] = b;
        p.st = S_LEN;
        return ERR_OK;

    case S_LEN:
        if (b < FRAME_LEN_MIN || (uint16_t)b > FRAME_LEN_MAX) {
            return ERR_BAD_LEN;
        }
        p.buf[p.idx++] = b;
        p.len  = b;
        p.need = (uint8_t)(b - FRAME_HDR_LEN);
        p.st   = S_CMD;
#ifdef DEBUG
	// len似乎不包含2字节的crc
	configASSERT(p.len == (rx_dma_total() - (read_total - 2) - 2));
#endif
        return ERR_OK;

    case S_CMD:
        if (b != CMD_HOST && b != CMD_SLAVE) {
            return ERR_BAD_CMD;
        }
        p.buf[p.idx++] = b;
        p.st = S_SUB;
        return ERR_OK;

    case S_SUB:
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
        got  = (uint16_t)p.buf[p.len]
               | ((uint16_t)p.buf[p.len + 1U] << 8);
        calc = CrcValueCalc(p.buf, p.len);

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
        parse_err_t e = parse_feed(rxbuffer[pos]);

        /* 这一帧有问题：整帧丢弃，等主机重发 */
        if (e != ERR_OK) {
            goto out;
        }
        if (++pos == RXBUF_SIZE)
            pos = 0;
	/* 每消费一个字节只在这里推进 */
        read_total++;

        /* 一帧成功交付，本次突发后面的字节当噪声丢掉 */
        if (p.done) {
#ifdef DEBUG
		configASSERT(p.len + 2 <= FRAME_MAX_SIZE);
#endif
            e = on_frame(p.buf, (uint16_t)(p.len + 2U));
	    if (e != ERR_OK)
		    goto out;
        }
    }

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

out:
    read_total = rx_dma_total();
    return e;
}

parse_err_t on_frame(const uint8_t *frame, uint16_t len)
{
	uint8_t sub_cmd = frame[4];
	parse_err_t e = ERR_OK;
#ifdef DEBUG
	configASSERT(frame[3] == 0xf1);
	configASSERT(0x01 <= sub_cmd && sub_cmd <= 0x08);
#endif
	// switch (sub_cmd) {
	// case 0x01:
	//
	// case 0x02:
	// case 0x03:
	// case 0x04:
	// case 0x05:
	// case 0x06:
	// case 0x06:
	// case 0x07:
	// case 0x08:
	// }
	//
out:
	return e;
}
