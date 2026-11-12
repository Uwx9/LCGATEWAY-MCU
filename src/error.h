#ifndef ERROR_H
#define ERROR_H

/* process_all_available() 的返回值：0 表示成功，负数表示不同错误 */
typedef enum {
    ERR_OK        = 0,
    ERR_NO_HEAD   = -1,   /* 找帧头时丢掉了非帧头字节 */
    ERR_BAD_RW    = -2,   /* 第二字节 RW 非法 */
    ERR_BAD_LEN   = -3,   /* LEN 小于 5 或 LEN + 2 超出 RXBUF_SIZE */
    ERR_BAD_CMD   = -4,   /* CMD 既不是 CMD_HOST 也不是 CMD_SLAVE */
    ERR_CRC       = -5,   /* CRC 校验失败 */
    ERR_PARTIAL   = -6,   /* 一次 idle 突发结束时帧还不完整 */
    ERR_RX_MOVED  = -7,   /* 解析过程中 DMA 尾指针又前进了 */
    ERR_OVERRUN   = -8,   /* 未读数据已经被 DMA 覆盖 */
} parse_err_t;

#endif
