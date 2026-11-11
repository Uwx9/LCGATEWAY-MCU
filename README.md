GD32F310C8T6为8Kram + 64Krom

关于elf产物
第一个可执行段就在8M处，代码直接在这里执行
.data .bss 在flash中位于上面那个可执行段后面，但是会被加载到2M地址处.bss不占文件空间
.heap_stack紧跟其后，这个节不占文件空间

ram的使用情况，按顺序
.data 是有初值的变量
.bss 是FreeRTOS ucHeap 4K configTOTAL_HEAP_SIZE预留的堆 + 
代码定义的没有初始化的变量, .bss大约6K左右
.heap_stack是newlib malloc堆1K + MSP 栈2K
上面这种情况下超出ram大小了，可以把msp改为1K

启动流程: 
先从0x0000 0000地址获取栈顶值(_sp, 链接脚本中是msp栈的顶端)，再从
0x0000 0004地址获得引导代码的基地址，然后从引导代码的基地址开始执行程序
1. 复位，BOOT 选择主 Flash → Flash 别名到 0x00000000。
2. CPU 从 0x00000000 取 MSP = _sp，从 0x00000004 取 PC = Reset_Handler。
(flash开头节是.vectors 该节的头两个值就是_sp和Reset_Handler)
3. Reset_Handler 重设 SP为ram中的值，把 .data 从 Flash 拷到 RAM，把 .bss 清零。
.bss和.heap_stack不用拷贝，它们在文件中没有内容，只需要清理对应内存即可
4. SystemInit() 调用 nvic_vector_table_set(NVIC_VECTTAB_FLASH, 0)，即 SCB->VTOR =
0x08000000（system_gd32f3x0.c:307-310、gd32f3x0_misc.c:116-120）。此后中断直接按
Flash 的 0x08000000 取向量，不再依赖 0 地址别名。
5. 进入 main()。
不必担心访存是否正确，由于链接脚本，链接器知道各个节最终在内存中的位置

bin:
.bin文件单纯是可加载段，也不会额外补0

唯一需要copy的.data节的lma是链接脚本的_sidata,vma 是_sdata，这样就知道拷贝时的地址

TODO
使用EGGERRTT

