#ifndef MEMMAP_H
#define MEMMAP_H

#include <stdint.h>

/* app 链接在 0x08002000，长度为 60K；stage/metadata 地址见 layout.h */
uint32_t memmap_flash_start(void);
uint32_t memmap_flash_size(void);
uint32_t memmap_flash_end(void);
uint32_t memmap_image_end(void);
uint32_t memmap_image_size(void);
uint32_t memmap_data_size(void);

#endif
