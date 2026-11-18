主控
GD32F330CBT6 84MHZ 16K ram + 128K flash

app 区 60K，stage 60K
flash布局：
  0x08000000 + 8K   bootloader（前 6K 代码，后 2K metadata A/B）
  0x08002000 + 60K  app
  0x08011000 + 60K  stage

USART TX --- PA9
USART RX --- PA10

