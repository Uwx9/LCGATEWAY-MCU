#include "memmap.h"

/* 链接脚本导出的符号，取地址即为数值 */
extern char __flash_start[];
extern char __flash_size[];
extern char _sidata[];
extern char _sdata[];
extern char _edata[];

uint32_t memmap_flash_start(void)
{
    return (uint32_t)__flash_start;
}

uint32_t memmap_flash_size(void)
{
    return (uint32_t)__flash_size;
}

uint32_t memmap_flash_end(void)
{
    return memmap_flash_start() + memmap_flash_size();
}

uint32_t memmap_data_size(void)
{
    return (uint32_t)(_edata - _sdata);
}

/* 这里认为从vector到data节都是image, */
uint32_t memmap_image_end(void)
{
    return (uint32_t)_sidata + memmap_data_size();
}

uint32_t memmap_image_size(void)
{
    return memmap_image_end() - memmap_flash_start();
}
