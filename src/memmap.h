#ifndef MEMMAP_H
#define MEMMAP_H

#include <stdint.h>

/* 链接脚本导出的 flash 布局；镜像末尾即 .data 初值在 flash 中的末尾 */
uint32_t memmap_flash_start(void);
uint32_t memmap_flash_size(void);
uint32_t memmap_flash_end(void);
uint32_t memmap_image_end(void);
uint32_t memmap_image_size(void);
uint32_t memmap_data_size(void);

#endif
