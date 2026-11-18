#ifndef DIDO_H
#define DIDO_H

#include <stdint.h>

/*
 * 设置数字输出。
 * DO_id: 逻辑编号，从 1 开始（默认 DO1~DO16）
 * status: 0 = 输出低电平，1 = 输出高电平
 * 返回: 0 成功，1 status 非法，2 DO_id 非法
 */
int DO_set(uint8_t DO_id, uint8_t status);

#endif /* DIDO_H */
