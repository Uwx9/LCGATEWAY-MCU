/*
 * 测试2:
 * 测试方法: 主机每次发送99字节长的一帧
 * 测试目标: 统计3者溢出次数, dmawrap溢出>=1次, 每一次process中计算avail都应该是99
 *           且每一帧都得到正确响应
 * 测试目的: 证明avail不受dma_total, read_total, dmawrap(计算dma_total使用)溢出的影响
 *           只要主机一次性不传递超过2^32个字节mcu总是能得到这次传输正确的字节数
 */
