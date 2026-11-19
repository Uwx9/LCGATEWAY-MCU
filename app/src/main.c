#include "FreeRTOS.h"
#include "gd32f3x0.h"
#include "task.h"
#include "parse.h"
#include "errcode.h"
#include "RTT_HELP.h"
#include "SEGGER_RTT.h"

#define USART0_RDATA_ADDRESS      ((uint32_t)&USART_RDATA(USART0))

uint8_t rxbuffer[RXBUF_SIZE];
volatile uint32_t rx_dma_wraps = 0;
volatile uint32_t dma_ch1_irq_count = 0;
volatile uint32_t dma_ch2_irq_count = 0;
volatile uint32_t usart0_idle_irq_count = 0;
volatile uint32_t dma_total_wraps = 0;      /* rx_dma_total() 的 32 位回绕次数 */
volatile uint32_t read_total_wraps = 0;     /* read_total 的 32 位回绕次数 */
volatile uint32_t rx_dma_wraps_wraps = 0;   /* rx_dma_wraps 的 32 位回绕次数 */
static void dma_rx_start_cmdmode(void);

void vApplicationStackOverflowHook(TaskHandle_t task, char *task_name)
{
    (void)task;
    (void)task_name;
    taskDISABLE_INTERRUPTS();
    for (;;) {
    }
}

/* 返回 DMA 已写入的绝对字节数；读数期间若发生套圈则重读 */
/* 解析时使用，主机不应该传数据 */
uint32_t rx_dma_total(void)
{
    uint32_t lap_a;
    uint32_t lap_b;
    uint32_t cnt;
    uint32_t pos;

    //TODO
    do {
        lap_a = rx_dma_wraps;
        cnt   = dma_transfer_number_get(DMA_CH2);
        lap_b = rx_dma_wraps;
    } while (lap_a != lap_b && cnt != 0);

    pos = (RXBUF_SIZE - cnt) % RXBUF_SIZE;
    return lap_a * RXBUF_SIZE + pos;
}

static void rx_task(void *arg)
{
    (void)arg;

    /* 先复位解析器，再启动 DMA */
    parse_init();
    dma_rx_start_cmdmode();

    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        parse_err_t ret = process_all_available();
        if (ret != ERR_OK) {
            SEGGER_RTT_printf(0, "[iap] err=%d\r\n", (int)ret);
        }
    }
}

/* 设置dmaCH2将串口的数据 '循环' 搬运到内存, 串口rx空闲时产生中断, 中断通知任务处理数据 */
static void dma_rx_start_cmdmode(void)
{
    dma_parameter_struct dma_init_struct;

    /* initialize DMA channel2 */
    dma_deinit(DMA_CH2);
    dma_struct_para_init(&dma_init_struct);
    dma_init_struct.direction    = DMA_PERIPHERAL_TO_MEMORY;
    dma_init_struct.memory_addr  = (uint32_t)rxbuffer;
    dma_init_struct.memory_inc   = DMA_MEMORY_INCREASE_ENABLE;
    dma_init_struct.memory_width = DMA_MEMORY_WIDTH_8BIT;
    dma_init_struct.number       = RXBUF_SIZE;
    dma_init_struct.periph_addr  = (uint32_t)USART0_RDATA_ADDRESS;
    dma_init_struct.periph_inc   = DMA_PERIPH_INCREASE_DISABLE;
    dma_init_struct.periph_width = DMA_PERIPHERAL_WIDTH_8BIT;
    dma_init_struct.priority     = DMA_PRIORITY_ULTRA_HIGH;
    dma_init(DMA_CH2, &dma_init_struct);

    /* configure DMA mode */
    dma_circulation_enable(DMA_CH2);
    dma_memory_to_memory_disable(DMA_CH2);

    /* 搬满一圈产生中断，用于统计绝对写入量、检测 overrun */
    dma_interrupt_enable(DMA_CH2, DMA_INT_FTF);
    /* 靠空闲中断叫醒 CPU */
    usart_interrupt_enable(USART0, USART_INT_IDLE);

    /* 抢占优先级5, 子优先级0；不高于 configMAX_SYSCALL_INTERRUPT_PRIORITY */
    nvic_irq_enable(USART0_IRQn, 5, 0);
    nvic_irq_enable(DMA_Channel1_2_IRQn, 6, 0);

    /* enable DMA channel2 */
    dma_channel_enable(DMA_CH2);
}

static void com_usart_init(void)
{
    /* enable COM GPIO clock */
    rcu_periph_clock_enable(RCU_GPIOA);
    /* enable USART clock */
    rcu_periph_clock_enable(RCU_USART0);

    /* connect port to USART TX */
    gpio_af_set(GPIOA, GPIO_AF_1, GPIO_PIN_9);

    /* connect port to USART RX */
    gpio_af_set(GPIOA, GPIO_AF_1, GPIO_PIN_10);

    /* configure USART Tx as alternate function push-pull */
    gpio_mode_set(GPIOA, GPIO_MODE_AF, GPIO_PUPD_PULLUP, GPIO_PIN_9);
    gpio_output_options_set(GPIOA, GPIO_OTYPE_PP, GPIO_OSPEED_10MHZ, GPIO_PIN_9);

    /* configure USART Rx as alternate function push-pull */
    gpio_mode_set(GPIOA, GPIO_MODE_AF, GPIO_PUPD_PULLUP, GPIO_PIN_10);
    gpio_output_options_set(GPIOA, GPIO_OTYPE_PP, GPIO_OSPEED_10MHZ, GPIO_PIN_10);

    /* configure USART */
    usart_deinit(USART0);
    usart_word_length_set(USART0, USART_WL_8BIT);
    usart_stop_bit_set(USART0, USART_STB_1BIT);
    usart_parity_config(USART0, USART_PM_NONE);
    usart_baudrate_set(USART0, 115200U);
    usart_receive_config(USART0, USART_RECEIVE_ENABLE);
    usart_transmit_config(USART0, USART_TRANSMIT_ENABLE);
    /* enable USART DMA for transmission */
    usart_dma_transmit_config(USART0, USART_TRANSMIT_DMA_ENABLE);
    /* enable USART DMA for reception */
    usart_dma_receive_config(USART0, USART_RECEIVE_DMA_ENABLE);
    usart_enable(USART0);
}

/*
 * 开启dma时钟, 初始化usart
 */
static void dma_usart_prepare(void)
{
    /* enable DMA clock */
    rcu_periph_clock_enable(RCU_DMA);
    /* initialize the com */
    com_usart_init();
}

TaskHandle_t rx_task_handle;
int main(void)
{
    /* RTT 控制块只在这里初始化一次，后续 API 会跳过重复初始化 */
    SEGGER_RTT_Init();

    /* 1. 优先级分组必须在所有 nvic_irq_enable 之前 */
    nvic_priority_group_set(NVIC_PRIGROUP_PRE4_SUB0);

    /* 2. 时钟、GPIO、USART 基础配置 */
    dma_usart_prepare();

    /* 3. 创建任务 */
    BaseType_t ret = xTaskCreate(rx_task, "rx", 256, NULL, 4, &rx_task_handle);
    configASSERT(ret == pdPASS);

    /* 低优先级任务，轮询主机经 RTT 下发的命令 */
    rtt_cmd_task_start();

    /* led */
    // rcu_periph_clock_enable(RCU_GPIOC);
    // gpio_mode_set(GPIOC, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO_PIN_13);
    // gpio_output_options_set(GPIOC, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ, GPIO_PIN_13);
    // gpio_bit_set(GPIOC, GPIO_PIN_13);

    /* 正常不返回 */
    vTaskStartScheduler();

    for (;;) {
    }
}
