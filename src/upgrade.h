#ifndef UPGRADE_H
#define UPGRADE_H

#include <stdint.h>

/*
 * flash 布局（128K）：
 *   0x08000000 + 60K  当前固件
 *   0x0800F000 + 8K   upgrade 常驻代码（本文件）
 *   0x08011000 + 60K  新固件暂存区
 */
#define UPGRADE_IMAGE_SIZE   (60U * 1024U)
#define UPGRADE_CODE_SIZE    (8U * 1024U)
#define UPGRADE_PAGE_SIZE    1024U   /* 与 operats.c 的 FLASH_PAGE_SIZE 一致 */

/*
 * 关中断，把 stage 的 60K 按页拷到 front，然后复位。
 * 不校验、不回滚、不保留旧前区，不返回。
 */
void upgrade_apply(uint32_t front, uint32_t stage) __attribute__((noreturn));

#endif /* UPGRADE_H */
