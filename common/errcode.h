#ifndef ERRCODE_H
#define ERRCODE_H

/*
 * IAP 协议公共定义：帧格式、命令码、ACK 和所有错误码。
 * app、bootloader 下载器和 host 测试工具共用这一个头文件。
 *
 * 设备上线协议错误码统一用正数，直接作为 0x8F 错误帧的 err_code；
 * app 解析层内部错误用 100+，只用于本地流程/RTT，不上线；
 * host 测试工具的本地返回码仍按 C 约定用 0/负数，它们不是设备协议错误码。
 */

#include <stdint.h>

/* 帧格式：
 *   [0]       HEAD 0x40
 *   [1]       LEN  整帧字节数（含 HEAD、LEN、CMD、DATA、CRC）
 *   [2]       CMD
 *   [3..]     DATA
 *   [LEN-2]   CRC_L
 *   [LEN-1]   CRC_H
 * CRC16 覆盖 HEAD 到最后一个 DATA 字节。
 */
#define IAP_FRAME_HEAD       0x40U
#define IAP_FRAME_DATA_OFF   3U
#define IAP_FRAME_OVERHEAD   5U   /* HEAD + LEN + CMD + CRC */
#define IAP_FRAME_LEN_MIN    IAP_FRAME_OVERHEAD
#define IAP_FRAME_DATA_MAX   130U /* 2 字节包号 + 最多 128 字节数据 */
#define IAP_FRAME_LEN_MAX    (IAP_FRAME_OVERHEAD + IAP_FRAME_DATA_MAX)

#define IAP_CMD_SET_DO       0x01U
#define IAP_CMD_READ_DI      0x02U
#define IAP_CMD_START        0x03U
#define IAP_CMD_FILE_SIZE    0x04U
#define IAP_CMD_ERASE        0x05U
#define IAP_CMD_DATA         0x06U
#define IAP_CMD_PACKETS      0x07U
#define IAP_CMD_TOTAL_CRC    0x08U
#define IAP_CMD_FINISH       0x09U
#define IAP_CMD_BOOT_STATUS  0x0AU

#define IAP_ACK(cmd)         ((uint8_t)((cmd) | 0x80U))
/* 语义错误帧：DATA = 原命令 + err_code */
#define IAP_ACK_ERR          0x8FU

/* 协议语义错误码：1..15，直接放进 0x8F 帧 */
typedef enum {
    IAP_OK          = 0,
    IAP_ERR_STATE   = 1,
    IAP_ERR_PARAM   = 2,
    IAP_ERR_SIZE    = 3,
    IAP_ERR_SEQ     = 4,
    IAP_ERR_FLASH   = 5,
    IAP_ERR_CRC     = 6,
    IAP_ERR_VERSION = 7,
    IAP_ERR_TX      = 8,
    IAP_ERR_SET_DO  = 9,
    IAP_ERR_READ_DI = 10,
    IAP_ERR_COUNT   = 11,
    IAP_ERR_OTHER   = 15,
} iap_err_t;

/* app 解析器/命令处理返回码 */
typedef enum {
    /* 帧解析错误码 */
    ERR_OK          = 0,
    ERR_NO_HEAD     = 100,  /* 找帧头时丢掉了非帧头字节 */
    ERR_BAD_LEN     = 101,  /* LEN 小于 5 或大于 FRAME_LEN_MAX */
    ERR_BAD_CMD     = 102,  /* CMD 不是合法的 IAP 主机指令 */
    ERR_CRC         = 103,  /* 帧 CRC 校验失败 */
    ERR_PARTIAL     = 104,  /* 一次 idle 突发结束时帧还不完整 */
    ERR_RX_MOVED    = 105,  /* 解析过程中 DMA 尾指针又前进了 */
    ERR_OVERRUN     = 106,  /* 未读数据已经被 DMA 覆盖 */
    ERR_options     = 107,  /* 执行动作失败 */
    /* 动作错误码 */
    ERR_IAP_STATE   = IAP_ERR_STATE,
    ERR_IAP_PARAM   = IAP_ERR_PARAM,
    ERR_IAP_SIZE    = IAP_ERR_SIZE,
    ERR_IAP_SEQ     = IAP_ERR_SEQ,
    ERR_IAP_FLASH   = IAP_ERR_FLASH,
    ERR_IAP_CRC     = IAP_ERR_CRC,
    ERR_IAP_TX      = IAP_ERR_TX,
    ERR_IAP_VERSION = IAP_ERR_VERSION,
    ERR_IAP_SET_DO  = IAP_ERR_SET_DO,
    ERR_IAP_READ_DI = IAP_ERR_READ_DI,
    ERR_IAP_COUNT   = IAP_ERR_COUNT,
} parse_err_t;

/* host 测试工具本地返回码，不是线上协议错误码 */
#define ACK_OK           0
#define ACK_ERR_TIMEOUT  (-1)
#define ACK_ERR_IO       (-2)
#define ACK_ERR_CMD      (-3)
#define ACK_ERR_LEN      (-4)
#define ACK_ERR_DATA     (-5)
#define ACK_ERR_DEVICE   (-6)

#endif /* ERRCODE_H */
