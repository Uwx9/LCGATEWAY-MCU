#ifndef PARSE_H
#define PARSE_H

#include <stdint.h>
#include "parse-error.h"

/* test5 的固件测试命令（0x7E）；生产固件注释掉这一行 */
#define TEST5

#define FRAME_HEAD      0x40
#define FRAME_RW        0x06
#define CMD_HOST        0xF1
#define CMD_SLAVE       0xF2

/* RXBUF_SIZE 必须为2的n次方 */
#define RXBUF_SIZE      256U

/*
 * 帧格式（多字节字段均为小端）：
 *   [0]    HEAD 0x40
 *   [1]    RW   0x06
 *   [2]    LEN  从 HEAD 到最后一个数据字节的字节数，即 5 + 数据长度
 *   [3]    CMD
 *   [4]    SUB
 *   [5..]  DATA 包号(2 字节) + 数据(最多 128 字节)
 *   [LEN]  CRC_L
 *   [LEN+1]CRC_H
 *
 * LEN 范围 5..135，整帧长度 LEN + 2 不超过 137。
 */
#define FRAME_HDR_LEN   5U
#define FRAME_MAX_SIZE	137U

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

#ifdef TEST5
/* 测试钩子（0x7E 指令）：预置/回读回绕计数 */
uint32_t parse_test_preset(uint32_t lap);
void parse_test_counters(uint32_t out[5]);
#endif

#endif
