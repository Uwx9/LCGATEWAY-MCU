/*
 * 测试方法 每次主机传输<=buffersize大小的数据量并开头有一帧, 后面填充0或ff
 * buffersize应该至少256字节，一帧最长为135字节
 * 希望快点溢出就buffersize, 健壮性考虑就都测试一下
 * 让dmawrap快速增加，这样dma_total 和 read_total会快速溢出，然后dmawrap也会溢出
 * 统计dma_total和read_total和dam_wrap的溢出次数,对比dma_wrap溢出前后的avail值证明dmawrap溢出不影响
 * 在这个实验里，avail应该一直都是buffersize，因为我们一次传输buufersize大小的数据
 * 当然要求每一帧都正确处理
 *
 */ 


/*
 * 测试1:
 * 测试方法: 主机每次发送99字节长的一帧
 * 测试目标: 统计3者溢出次数(64位), dmawrap溢出>=1次, 每一次process中计算avail都应该是99
 *			 且每一帧都得到正确响应
 * 测试目地: 证明avail不受dma_total, read_total, dmawrap(计算dma_total使用)溢出的影响
 *			 只要主机一次性不传递超过2^32个字节mcu总是能得到这次传输正确的字节数
 */
 
/*
 * 测试2
 * 测试方法: 主机所有可能发出的帧依次挨个发送
 * 测试目标: 统计3者溢出次数(64位), dmawrap溢出>=1次, (没法提前知道avail, 只能靠每次正确解析帧)
 *			 维护一个64位帧错误计数器 要求没有一帧错误
 * 测试目地: 验证真实情况下主机发送不同帧时, mcu可以正常解析			 
 */
