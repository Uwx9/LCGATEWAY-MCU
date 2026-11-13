#ifndef PARSE_H
#define PARSE_H

#include <stdint.h>

#include "parse-error.h"

#define FRAME_HEAD      0x40
#define FRAME_RW        0x06
#define CMD_HOST        0xF1
#define CMD_SLAVE       0xF2

/*
 * 帧格式（多字节字段均为小端）：
 *   [0]    HEAD 0x40
 *   [1]    RW   0x06
 *   [2]    LEN  从 HEAD 到最后一个数据字节的字节数，即 5 + 数据长度
 *   [3]    CMD
 *   [4]    SUB
 *   [5..]  DATA
 *   [LEN]  CRC_L
 *   [LEN+1]CRC_H
 *
 * LEN 范围 5..254，整帧长度 LEN + 2 不超过 RXBUF_SIZE。
 */
#define FRAME_HDR_LEN   5U

/* RXBUF_SIZE 必须为2的n次方 */
#define RXBUF_SIZE      256U

/* 初始化解析器（必须在启动 DMA 接收之前调用） */
void parse_init(void);

/*
 * 解析 DMA 环形缓冲中 [已读位置, DMA 当前写入位置) 的所有字节。
 * 成功返回 ERR_OK，出错返回 error.h 中对应的错误码。
 * 返回值只报告本次调用遇到的第一类错误。
 */
parse_err_t process_all_available(void);

/*
 * 完整且 CRC 正确的帧回调，frame 指向解析器内部缓冲，len 为整帧长度。
 * 默认实现为空，应用可在别处重新定义（弱符号）。
 */
void on_frame(const uint8_t *frame, uint16_t len);

#endif
