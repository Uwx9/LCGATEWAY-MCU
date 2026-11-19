主控
GD32F330CBT6 84MHZ 16K ram + 128K flash

app 区 50K，stage 50K
flash布局：
  0x08000000 + 28K  bootloader（前 26K 代码，后 2K metadata A/B）
  0x08007000 + 50K  app
  0x08013800 + 50K  stage

USART TX --- PA9
USART RX --- PA10

启动流程：
  bootloader 校验 metadata 和 app，正常则跳 app。
  app 无法启动（向量表非法、镜像 CRC 不符、拷贝失败、stage 缺失）时，
  先按 metadata 里的 prev_size/prev_crc 校验原 app，完整则回退运行原 app；
  否则停在串口下载模式：USART0 115200 8N1，帧协议与 app 侧 IAP 相同。
  镜像先写 stage，0x09 完成后写 metadata REQUEST 并复位，
  再由现有 stage -> app 流程完成拷贝和校验。

下载模式查询：
  boot 进入下载模式时会在 USART0 打印 "\r\n[boot] download mode\r\n"。
  协议查询：0x0A -> 0x8A 01 表示 bootloader 下载模式；app 不实现 0x0A。
  主机工具：tests/build/test_boot_status /dev/ttyUSB0

