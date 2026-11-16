#include "FreeRTOS.h"
#include "task.h"

#include "gd32f3x0.h"
#include "SEGGER_RTT.h"

#include "rtt_help.h"

#define REPORT_TASK_MAX   8U
#define REPORT_IRQ_MAX    64U

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

static void report_tasks(void)
{
    TaskStatus_t tasks[REPORT_TASK_MAX];
    UBaseType_t listed;
    UBaseType_t total = uxTaskGetNumberOfTasks();

    listed = uxTaskGetSystemState(tasks, REPORT_TASK_MAX, NULL);

    SEGGER_RTT_printf(0, "[task] sched=%s total=%u listed=%u\r\n",
                      scheduler_state_name(xTaskGetSchedulerState()),
                      (unsigned)total, (unsigned)listed);
    SEGGER_RTT_printf(0, "name     state prio base hw  handle\r\n");

    for (UBaseType_t i = 0U; i < listed; i++) {
        SEGGER_RTT_printf(0, "%-8.8s %-5s %2u/%-2u %3u %08x\r\n",
                          tasks[i].pcTaskName,
                          task_state_name(tasks[i].eCurrentState),
                          (unsigned)tasks[i].uxCurrentPriority,
                          (unsigned)tasks[i].uxBasePriority,
                          (unsigned)tasks[i].usStackHighWaterMark,
                          (unsigned)(uint32_t)tasks[i].xHandle);
    }
}

static void report_irqs(void)
{
    unsigned enabled = 0U;

    SEGGER_RTT_printf(0, "[irq] group=%u max_syscall=0x%02x\r\n",
                      (unsigned)NVIC_GetPriorityGrouping(),
                      (unsigned)configMAX_SYSCALL_INTERRUPT_PRIORITY);

    for (uint32_t irq = 0U; irq < REPORT_IRQ_MAX; irq++) {
        uint32_t prio;
        uint32_t pre;

        if ((NVIC->ISER[irq >> 5U] & (1UL << (irq & 31U))) == 0U) {
            continue;
        }

        enabled++;
        /* NVIC->IP 是 8 位原始优先级；NVIC_GetPriority() 返回的是右对齐值 */
        prio = NVIC->IP[irq];
        pre = prio >> (8U - __NVIC_PRIO_BITS);

        SEGGER_RTT_printf(0, "irq %2u %-14.14s p=%02x pre=%u%s\r\n",
                          (unsigned)irq, irq_name(irq),
                          (unsigned)prio, (unsigned)pre,
                          (prio < configMAX_SYSCALL_INTERRUPT_PRIORITY) ? " !" : "");
    }

    SEGGER_RTT_printf(0, "[irq] enabled=%u\r\n", enabled);
}

void report_system_state(const char *tag)
{
    SEGGER_RTT_printf(0, "\r\n===== %s =====\r\n", (tag != NULL) ? tag : "report");
    report_tasks();
    report_irqs();
}
