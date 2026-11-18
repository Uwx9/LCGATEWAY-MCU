/*
 * 测试: DMA 绝对计数溢出（test_dma_overflow）
 * 状态: 占位，尚未实现。
 *
 * 测试方法: 主机每次发送 99 字节长的一帧
 * 测试目标: read_total 和 dma_total 都溢出至少一次，每次 process 计算的 avail
 *           都应该是 99，且每一帧都得到正确响应
 * 测试目的: 证明 avail 不受 dma_total、read_total、dma_wrap 溢出的影响；
 *           只要主机一次性不传超过 2^32 字节，MCU 总能得到正确的突发长度
 */
