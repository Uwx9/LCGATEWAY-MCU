#ifndef PARSE_H
#define PARSE_H

#include <stdint.h>
#include "errcode.h"

/* DMA 回绕测试（tests/test_dma_wrap.c）的固件测试命令（0x7E）；生产固件注释掉这一行 */
#define TEST5

#ifdef TEST5
/* 测试命令及其应答 */
#define TEST_CMD            0x7E

/* 测试钩子（0x7E 指令）：预置/回读回绕计数 */
uint32_t parse_test_preset(uint32_t lap);
void parse_test_counters(uint32_t out[5]);
#endif

#define FRAME_HEAD      IAP_FRAME_HEAD

/*
 * 帧格式（LEN 为 1 字节；版本/大小/包号/总校验等多字节数据大端，
 * CRC 低字节在前）：
 *   [0]       HEAD 0x40
 *   [1]       LEN  整帧字节数，包含 HEAD、LEN、CMD、DATA 和 CRC
 *   [2]       CMD  主机指令 0x01~0x09，MCU 应答为 CMD | 0x80
 *   [3..]     DATA
 *   [LEN-2]   CRC_L
 *   [LEN-1]   CRC_H
 *
 * LEN = 5 + 数据字节数，范围 5..135（数据包最多 2+128 字节）。
 * CRC16 覆盖 [0, LEN-3]，即从 HEAD 到最后一个数据字节。
 */
#define FRAME_DATA_OFF  IAP_FRAME_DATA_OFF
#define FRAME_OVERHEAD  IAP_FRAME_OVERHEAD
#define FRAME_LEN_MIN   IAP_FRAME_LEN_MIN
#define FRAME_DATA_MAX  IAP_FRAME_DATA_MAX
#define FRAME_LEN_MAX   IAP_FRAME_LEN_MAX

/* 主机指令、IAP_ACK()、IAP_ACK_ERR 及错误码见 common/errcode.h */

/* RXBUF_SIZE 必须为2的n次方 */
#define RXBUF_SIZE      256U

/*
 * 解析器缓冲按整帧大小分配，LEN 最大为 FRAME_LEN_MAX，
 * 这里仍按 RXBUF_SIZE 给足余量。
 */
#define FRAME_MAX_SIZE  FRAME_LEN_MAX

/* 初始化解析器（必须在启动 DMA 接收之前调用） */
void parse_init(void);

/* DMA 环的写指针（绝对字节数） */
uint32_t rx_dma_total(void);

/* 解析器已消费的绝对字节数 */
uint32_t parse_read_total(void);

/*
 * 解析 DMA 环形缓冲中 [已读位置, DMA 当前写入位置) 的所有字节。
 * 成功返回 ERR_OK，出错返回 error.h 中对应的错误码。
 * 返回值只报告本次调用遇到的第一类错误。
 */
parse_err_t process_all_available(void);

/* 完整且 CRC 正确的帧回调, frame 指向解析器内部缓冲, len 为整帧长度 */
parse_err_t on_frame(const uint8_t *frame, uint16_t len);

/* crc16（长度用 32 位，整包校验和单帧校验共用） */
uint16_t CrcValueCalc(const uint8_t *cpucData, uint32_t len);

#endif
