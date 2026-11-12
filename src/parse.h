#ifndef PARSE_H
#define PARSE_H

#include <stdint.h>

#define FRAME_HEAD      0x40
#define FRAME_RW        0x06
#define CMD_HOST        0xF1
#define CMD_SLAVE       0xF2

/* 最大帧：128 字节数据 + 5 字节头 + 2 字节包号 + 2 字节 CRC，留点余量 */
#define FRAME_MAX       160

#endif
