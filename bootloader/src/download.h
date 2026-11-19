#ifndef BOOT_DOWNLOAD_H
#define BOOT_DOWNLOAD_H

/*
 * app 无法启动时进入的串口下载模式，不返回。
 * 协议与 app 侧 IAP 相同（USART0, 115200 8N1），镜像写入 stage，
 * 收到 0x09 后写 metadata REQUEST 并复位，由现有状态机完成拷贝。
 */
void download_mode(void) __attribute__((noreturn));

#endif /* BOOT_DOWNLOAD_H */
