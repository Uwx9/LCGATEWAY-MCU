#include "FreeRTOS.h"
#include "task.h"

#include <string.h>

#include "gd32f3x0.h"
#include "SEGGER_RTT.h"

#include "rtt_help.h"

#define REPORT_TASK_MAX   8U
#define REPORT_IRQ_MAX    64U

#define MSP_PAINT_BYTE    0xA5U

#define ANSI_RESET    "\x1b[0m"
#define ANSI_HEAD     "\x1b[1;36m"
#define ANSI_BOLD     "\x1b[1m"
#define ANSI_DIM      "\x1b[2m"
#define ANSI_GREEN    "\x1b[32m"
#define ANSI_CYAN     "\x1b[36m"
#define ANSI_YELLOW   "\x1b[33m"
#define ANSI_MAGENTA  "\x1b[35m"
#define ANSI_RED      "\x1b[1;31m"

/* 链接脚本导出的内存布局符号 */
extern char _sidata[];
extern char _sdata[];
extern char _edata[];
extern char _sbss[];
extern char _ebss[];
extern char _end[];
extern char _heap_end[];
extern char _sp[];
extern char _etext[];
extern char __Vectors_End[];
extern char __fw_version_str_start[];
extern char __fw_version_num_end[];
extern char __text_start[];
extern char __rodata_start[];
extern char __rodata_end[];
extern char __exidx_start[];
extern char __exidx_end[];
extern char __init_array_start[];
extern char __init_array_end[];
extern char __fini_array_start[];
extern char __fini_array_end[];
extern char __flash_start[];
extern char __flash_size[];
extern char __ram_start[];
extern char __ram_size[];

/* newlib 的 sbrk 当前指针 */
extern char *_sbrk_cur(void);

/* main.c 中的 DMA/USART 统计 */
extern volatile uint32_t rx_dma_wraps;
extern volatile uint32_t dma_ch1_irq_count;
extern volatile uint32_t dma_ch2_irq_count;
extern volatile uint32_t usart0_idle_irq_count;
extern volatile uint32_t dma_total_wraps;
extern volatile uint32_t read_total_wraps;
extern volatile uint32_t rx_dma_wraps_wraps;

typedef struct {
    uint8_t irq;
    const char *name;
} irq_name_t;

static const irq_name_t g_irq_names[] = {
    {WWDGT_IRQn,               "WWDGT"},
    {LVD_IRQn,                 "LVD"},
    {RTC_IRQn,                 "RTC"},
    {FMC_IRQn,                 "FMC"},
    {RCU_CTC_IRQn,             "RCU_CTC"},
    {EXTI0_1_IRQn,             "EXTI0_1"},
    {EXTI2_3_IRQn,             "EXTI2_3"},
    {EXTI4_15_IRQn,            "EXTI4_15"},
    {TSI_IRQn,                 "TSI"},
    {DMA_Channel0_IRQn,        "DMA_CH0"},
    {DMA_Channel1_2_IRQn,      "DMA_CH1_2"},
    {DMA_Channel3_4_IRQn,      "DMA_CH3_4"},
    {ADC_CMP_IRQn,             "ADC_CMP"},
    {TIMER0_BRK_UP_TRG_COM_IRQn, "TIMER0_BRK"},
    {TIMER0_Channel_IRQn,      "TIMER0_CH"},
    {TIMER1_IRQn,              "TIMER1"},
    {TIMER2_IRQn,              "TIMER2"},
    {TIMER13_IRQn,             "TIMER13"},
    {TIMER14_IRQn,             "TIMER14"},
    {TIMER15_IRQn,             "TIMER15"},
    {TIMER16_IRQn,             "TIMER16"},
    {I2C0_EV_IRQn,             "I2C0_EV"},
    {I2C1_EV_IRQn,             "I2C1_EV"},
    {SPI0_IRQn,                "SPI0"},
    {SPI1_IRQn,                "SPI1"},
    {USART0_IRQn,              "USART0"},
    {USART1_IRQn,              "USART1"},
    {I2C0_ER_IRQn,             "I2C0_ER"},
    {I2C1_ER_IRQn,             "I2C1_ER"},
    {DMA_Channel5_6_IRQn,      "DMA_CH5_6"},
};

static const char *irq_name(uint32_t irq)
{
    unsigned i;

    for (i = 0U; i < sizeof(g_irq_names) / sizeof(g_irq_names[0]); i++) {
        if (g_irq_names[i].irq == irq) {
            return g_irq_names[i].name;
        }
    }

    return "UNKNOWN";
}

static const char *task_state_name(eTaskState state)
{
    switch (state) {
    case eRunning:
        return "RUN";
    case eReady:
        return "READY";
    case eBlocked:
        return "BLOCK";
    case eSuspended:
        return "SUSP";
    case eDeleted:
        return "DEL";
    default:
        return "?";
    }
}

static const char *task_state_color(eTaskState state)
{
    switch (state) {
    case eRunning:
        return ANSI_GREEN;
    case eReady:
        return ANSI_CYAN;
    case eBlocked:
        return ANSI_YELLOW;
    case eSuspended:
        return ANSI_MAGENTA;
    default:
        return ANSI_RED;
    }
}

static const char *scheduler_state_name(BaseType_t state)
{
    switch (state) {
    case taskSCHEDULER_NOT_STARTED:
        return "NOT_STARTED";
    case taskSCHEDULER_RUNNING:
        return "RUNNING";
    case taskSCHEDULER_SUSPENDED:
        return "SUSPENDED";
    default:
        return "?";
    }
}

static void put_spaces(unsigned count)
{
    static const char spaces[] = "                ";

    while (count > 0U) {
        unsigned chunk = (count > (sizeof(spaces) - 1U)) ? (sizeof(spaces) - 1U) : count;

        SEGGER_RTT_Write(0, spaces, chunk);
        count -= chunk;
    }
}

/* SEGGER_RTT_printf 的 %s 忽略字段宽度，字符串列只能自己补空格 */
static void put_field(const char *text, unsigned width)
{
    unsigned len = 0U;

    while ((text[len] != '\0') && (len < width)) {
        len++;
    }
    if (len > 0U) {
        SEGGER_RTT_Write(0, text, len);
    }
    put_spaces(width - len);
}

static uintptr_t s_msp_paint_top;

/* 主栈区间为 [_heap_end, _sp)：首次报告时给未使用部分涂标记，之后扫描得到历史最小剩余 */
static void msp_paint(void)
{
    volatile uint8_t *p = (volatile uint8_t *)_heap_end;
    uintptr_t top = (uintptr_t)__get_MSP();

    while ((uintptr_t)p < top) {
        *p++ = MSP_PAINT_BYTE;
    }
    s_msp_paint_top = top;
}

static uint32_t msp_free_bytes(void)
{
    const volatile uint8_t *p = (const volatile uint8_t *)_heap_end;

    if (s_msp_paint_top == 0U) {
        return 0U;
    }
    while (((uintptr_t)p < s_msp_paint_top) && (*p == MSP_PAINT_BYTE)) {
        p++;
    }
    return (uint32_t)((uintptr_t)p - (uintptr_t)_heap_end);
}

static void report_io(void)
{
    SEGGER_RTT_printf(0, ANSI_HEAD "=============== counter ===============" ANSI_RESET "\r\n");
    SEGGER_RTT_printf(0, "wrap    dma=%u read=%u wraps=%u\r\n",
                      (unsigned)dma_total_wraps,
                      (unsigned)read_total_wraps,
                      (unsigned)rx_dma_wraps_wraps);
    SEGGER_RTT_printf(0, "irq     ch1=%u ch2=%u usart_idle=%u\r\n",
                      (unsigned)dma_ch1_irq_count,
                      (unsigned)dma_ch2_irq_count,
                      (unsigned)usart0_idle_irq_count);
}

static void report_memory(void)
{
    uint32_t flash_total = (uint32_t)__flash_size;
    uint32_t flash_used = (uint32_t)(_sidata - __flash_start) +
                          (uint32_t)(_edata - _sdata);
    uint32_t vectors_size = (uint32_t)(__Vectors_End - __flash_start);
    uint32_t version_size = (uint32_t)(__fw_version_num_end - __fw_version_str_start);
    uint32_t text_size = (uint32_t)(_etext - __text_start);
    uint32_t rodata_size = (uint32_t)(__rodata_end - __rodata_start);
    uint32_t data_size = (uint32_t)(_edata - _sdata);
    uint32_t exidx_size = (uint32_t)(__exidx_end - __exidx_start);
    uint32_t initfini_size = (uint32_t)((__init_array_end - __init_array_start) +
                                        (__fini_array_end - __fini_array_start));
    uint32_t pad_size = flash_used - (vectors_size + version_size + text_size +
                                      rodata_size + data_size + exidx_size +
                                      initfini_size);
    uint32_t ram_total = (uint32_t)__ram_size;
    uint32_t ram_used = (uint32_t)(_sp - __ram_start);
    uint32_t bss_size = (uint32_t)(_ebss - _sbss);
    uint32_t heap_size = (uint32_t)(_heap_end - _end);
    uint32_t stack_size = (uint32_t)(_sp - _heap_end);
    uint32_t stack_free = msp_free_bytes();
    uint32_t rtos_free = (uint32_t)xPortGetFreeHeapSize();
    uint32_t libc_free = (uint32_t)(_heap_end - _sbrk_cur());

    SEGGER_RTT_printf(0, ANSI_HEAD "=============== storage ===============" ANSI_RESET "\r\n");

    SEGGER_RTT_printf(0, ANSI_BOLD "flash" ANSI_RESET
                      "   used=%5u/%-6u  free=" ANSI_GREEN "%u" ANSI_RESET "\r\n",
                      (unsigned)flash_used, (unsigned)flash_total,
                      (unsigned)(flash_total - flash_used));
    SEGGER_RTT_printf(0, "        " ANSI_DIM "vectors" ANSI_RESET "=%u "
                      ANSI_DIM "version" ANSI_RESET "=%u "
                      ANSI_DIM "text" ANSI_RESET "=%u\r\n",
                      (unsigned)vectors_size, (unsigned)version_size,
                      (unsigned)text_size);
    SEGGER_RTT_printf(0, "        " ANSI_DIM "rodata" ANSI_RESET "=%u "
                      ANSI_DIM "data(copy to ram)" ANSI_RESET "=%u "
                      ANSI_DIM "exidx" ANSI_RESET "=%u "
                      ANSI_DIM "init/fini" ANSI_RESET "=%u "
                      ANSI_DIM "pad" ANSI_RESET "=%u\r\n",
                      (unsigned)rodata_size, (unsigned)data_size,
                      (unsigned)exidx_size, (unsigned)initfini_size,
                      (unsigned)pad_size);

    SEGGER_RTT_printf(0, ANSI_BOLD "ram" ANSI_RESET
                      "     used=%5u/%-6u  free=" ANSI_GREEN "%u" ANSI_RESET "\r\n",
                      (unsigned)ram_used, (unsigned)ram_total,
                      (unsigned)(ram_total - ram_used));
    SEGGER_RTT_printf(0, "        " ANSI_DIM "data" ANSI_RESET "=%u "
                      ANSI_DIM "bss" ANSI_RESET "=%u "
                      ANSI_DIM "heap" ANSI_RESET "=%u "
                      ANSI_DIM "stack" ANSI_RESET "=%u\r\n",
                      (unsigned)data_size, (unsigned)bss_size,
                      (unsigned)heap_size, (unsigned)stack_size);

    SEGGER_RTT_printf(0, ANSI_BOLD "stack" ANSI_RESET
                      "   main free(min)=" ANSI_GREEN "%u" ANSI_RESET "/%u\r\n",
                      (unsigned)stack_free, (unsigned)stack_size);
    SEGGER_RTT_printf(0, ANSI_BOLD "heap" ANSI_RESET
                      "    rtos free=" ANSI_GREEN "%u" ANSI_RESET "/%u\r\n",
                      (unsigned)rtos_free, (unsigned)configTOTAL_HEAP_SIZE);
    SEGGER_RTT_printf(0, ANSI_BOLD "heap" ANSI_RESET
                      "    newlib free=" ANSI_GREEN "%u" ANSI_RESET "/%u\r\n",
                      (unsigned)libc_free, (unsigned)heap_size);
}

static void priority_bits(uint32_t group, unsigned *pre_bits, unsigned *sub_bits)
{
    uint32_t pre = ((7U - group) > __NVIC_PRIO_BITS) ? __NVIC_PRIO_BITS : (7U - group);
    uint32_t sub = ((group + __NVIC_PRIO_BITS) < 7U) ? 0U : (group - 7U + __NVIC_PRIO_BITS);

    *pre_bits = (unsigned)pre;
    *sub_bits = (unsigned)sub;
}

static void report_tasks(void)
{
    TaskStatus_t tasks[REPORT_TASK_MAX];
    UBaseType_t listed;
    UBaseType_t total = uxTaskGetNumberOfTasks();

    listed = uxTaskGetSystemState(tasks, REPORT_TASK_MAX, NULL);

    SEGGER_RTT_printf(0, ANSI_HEAD "================= tasks =================" ANSI_RESET "\r\n");
    SEGGER_RTT_printf(0, "sched=%s tasks=%u/%u\r\n",
                      scheduler_state_name(xTaskGetSchedulerState()),
                      (unsigned)total, (unsigned)listed);

    put_field("name", 10);
    SEGGER_RTT_printf(0, " ");
    put_field("state", 5);
    SEGGER_RTT_printf(0, " prio base stack  handle\r\n");

    for (UBaseType_t i = 0U; i < listed; i++) {
        put_field(tasks[i].pcTaskName, 10);
        SEGGER_RTT_printf(0, " ");
        SEGGER_RTT_printf(0, "%s", task_state_color(tasks[i].eCurrentState));
        put_field(task_state_name(tasks[i].eCurrentState), 5);
        SEGGER_RTT_printf(0, ANSI_RESET " %4u %4u %5u  0x%08x\r\n",
                          (unsigned)tasks[i].uxCurrentPriority,
                          (unsigned)tasks[i].uxBasePriority,
                          (unsigned)(tasks[i].usStackHighWaterMark * sizeof(StackType_t)),
                          (unsigned)(uint32_t)tasks[i].xHandle);
    }
}

static void report_irqs(void)
{
    uint32_t group = NVIC_GetPriorityGrouping();
    unsigned pre_bits;
    unsigned sub_bits;
    unsigned enabled = 0U;

    priority_bits(group, &pre_bits, &sub_bits);

    for (uint32_t irq = 0U; irq < REPORT_IRQ_MAX; irq++) {
        if ((NVIC->ISER[irq >> 5U] & (1UL << (irq & 31U))) != 0U) {
            enabled++;
        }
    }

    SEGGER_RTT_printf(0, ANSI_HEAD "=============== interrupts ===============" ANSI_RESET "\r\n");
    SEGGER_RTT_printf(0, "nvic group=%u pre_bits=%u sub_bits=%u max_syscall=0x%02x enabled=%u\r\n",
                      (unsigned)group, pre_bits, sub_bits,
                      (unsigned)configMAX_SYSCALL_INTERRUPT_PRIORITY, enabled);

    if (enabled == 0U) {
        SEGGER_RTT_printf(0, "(no interrupts enabled)\r\n");
        return;
    }

    for (uint32_t irq = 0U; irq < REPORT_IRQ_MAX; irq++) {
        uint32_t raw;
        uint32_t pre;
        uint32_t sub;

        if ((NVIC->ISER[irq >> 5U] & (1UL << (irq & 31U))) == 0U) {
            continue;
        }

        /* NVIC->IP 是 8 位原始优先级（左对齐），解码前先取右对齐值 */
        raw = NVIC->IP[irq];
        NVIC_DecodePriority(NVIC_GetPriority((IRQn_Type)irq), group, &pre, &sub);

        const char *flag = (raw < configMAX_SYSCALL_INTERRUPT_PRIORITY)
                           ? (" " ANSI_RED "unsafe" ANSI_RESET) : "";

        SEGGER_RTT_printf(0, "irq %2u  ", (unsigned)irq);
        put_field(irq_name(irq), 11);
        SEGGER_RTT_printf(0, " pre=%u sub=%u prio=0x%02x%s\r\n",
                          (unsigned)pre, (unsigned)sub, (unsigned)raw, flag);
    }
}

void report_system_state(const char *tag)
{
    if (s_msp_paint_top == 0U) {
        msp_paint();
    }

    SEGGER_RTT_printf(0, "\r\n %s\r\n", (tag != NULL) ? tag : "report");
    report_tasks();
    SEGGER_RTT_printf(0, "\r\n");
    report_irqs();
    SEGGER_RTT_printf(0, "\r\n");
    report_io();
    SEGGER_RTT_printf(0, "\r\n");
    report_memory();
}

#define RTT_CMD_MAX 16U

static char s_cmd[RTT_CMD_MAX];
static unsigned s_cmd_len;

static void rtt_poll_command(void)
{
    char c;

    while (SEGGER_RTT_Read(0, &c, 1U) == 1U) {
        if ((c == '\r') || (c == '\n')) {
            s_cmd[s_cmd_len] = '\0';
            if (strcmp(s_cmd, "ps") == 0) {
                report_system_state("ps");
            }
            s_cmd_len = 0U;
        } else if ((c == '\b') || (c == 0x7F)) {
            if (s_cmd_len > 0U) {
                s_cmd_len--;
            }
        } else if (s_cmd_len < (RTT_CMD_MAX - 1U)) {
            s_cmd[s_cmd_len++] = c;
        } else {
            s_cmd_len = 0U;
        }
    }
}

#define RTT_CMD_TASK_STACK 256U
#define RTT_CMD_POLL_MS    100U

static void rtt_cmd_task(void *arg)
{
    (void)arg;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(RTT_CMD_POLL_MS));
        rtt_poll_command();
    }
}

void rtt_cmd_task_start(void)
{
    BaseType_t ret = xTaskCreate(rtt_cmd_task, "rttcmd", RTT_CMD_TASK_STACK, NULL, 1, NULL);

    configASSERT(ret == pdPASS);
}
