#ifndef ERROR_H
#define ERROR_H

/* process_all_available() 的返回值：0 表示成功，负数表示不同错误 */
typedef enum {
    ERR_OK		= 0,
    ERR_NO_HEAD		= -1,   /* 找帧头时丢掉了非帧头字节 */
    ERR_BAD_LEN		= -3,   /* LEN 小于 5 或大于 FRAME_LEN_MAX */
    ERR_BAD_CMD		= -4,   /* CMD 不是合法的 IAP 主机指令 */
    ERR_CRC		= -5,   /* CRC 校验失败 */
    ERR_PARTIAL		= -6,   /* 一次 idle 突发结束时帧还不完整 */
    ERR_RX_MOVED	= -7,   /* 解析过程中 DMA 尾指针又前进了 */
    ERR_OVERRUN		= -8,   /* 未读数据已经被 DMA 覆盖 */
    ERR_options		= -9,   /* 执行动作失败 */
    ERR_IAP_STATE	= -10,  /* IAP 状态不允许当前动作 */
    ERR_IAP_PARAM	= -11,  /* 命令数据字段非法 */
    ERR_IAP_SIZE	= -12,  /* 文件大小超出剩余 flash */
    ERR_IAP_SEQ		= -13,  /* 数据包号不连续 */
    ERR_IAP_FLASH	= -14,  /* flash 擦除或编程失败 */
    ERR_IAP_CRC		= -15,  /* 总校验或包数不匹配 */
    ERR_IAP_TX		= -16,  /* 应答 DMA 发送超时 */
    ERR_IAP_VERSION	= -17,	/* 待升级版本不高于当前版本 */
    ERR_IAP_SET_DO	= -18,	/* 设置DO失败 */
    ERR_IAP_READ_DI	= -19,	/* 读取DI失败 */
} parse_err_t;

#endif
