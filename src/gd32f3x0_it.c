#include "FreeRTOS.h"
#include "gd32f3x0_it.h"
#include "task.h"

/*!
    \brief      this function handles NMI exception
    \param[in]  none
    \param[out] none
    \retval     none
*/
void NMI_Handler(void)
{
    /* if NMI exception occurs, go to infinite loop */
    while(1) {
    }
}

/*!
    \brief      this function handles HardFault exception
    \param[in]  none
    \param[out] none
    \retval     none
*/
void HardFault_Handler(void)
{
    /* if Hard Fault exception occurs, go to infinite loop */
    while(1) {
    }
}

/*!
    \brief      this function handles MemManage exception
    \param[in]  none
    \param[out] none
    \retval     none
*/
void MemManage_Handler(void)
{
    /* if Memory Manage exception occurs, go to infinite loop */
    while(1) {
    }
}

/*!
    \brief      this function handles BusFault exception
    \param[in]  none
    \param[out] none
    \retval     none
*/
void BusFault_Handler(void)
{
    /* if Bus Fault exception occurs, go to infinite loop */
    while(1) {
    }
}

/*!
    \brief      this function handles UsageFault exception
    \param[in]  none
    \param[out] none
    \retval     none
*/
void UsageFault_Handler(void)
{
    /* if Usage Fault exception occurs, go to infinite loop */
    while(1) {
    }
}

/*!
    \brief      this function handles DebugMon exception
    \param[in]  none
    \param[out] none
    \retval     none
*/
void DebugMon_Handler(void)
{
    /* if DebugMon exception occurs, go to infinite loop */
    while(1) {
    }
}

extern __IO FlagStatus g_transfer_complete;
extern volatile uint32_t rx_dma_wraps;
extern volatile uint32_t dma_ch1_irq_count;
extern volatile uint32_t dma_ch2_irq_count;
extern volatile uint32_t usart0_idle_irq_count;
extern volatile uint32_t dma_total_wraps;
extern volatile uint32_t rx_dma_wraps_wraps;
void DMA_Channel1_2_IRQHandler(void)
{
    if(RESET != dma_interrupt_flag_get(DMA_CH1, DMA_INT_FLAG_FTF)) {
        dma_interrupt_flag_clear(DMA_CH1, DMA_INT_FLAG_G);
        dma_ch1_irq_count++;
        g_transfer_complete = SET;
    }
    if(RESET != dma_interrupt_flag_get(DMA_CH2, DMA_INT_FLAG_FTF)) {
        dma_interrupt_flag_clear(DMA_CH2, DMA_INT_FLAG_G);
        rx_dma_wraps++;
        if(rx_dma_wraps == 0U) {
            rx_dma_wraps_wraps++;
        }
        /* dma_total = rx_dma_wraps * RXBUF_SIZE + pos，随 rx_dma_wraps 每 2^24 圈回绕一次 */
        if((rx_dma_wraps & 0x00FFFFFFU) == 0U) {
            dma_total_wraps++;
        }
        dma_ch2_irq_count++;
    }
}

extern TaskHandle_t rx_task_handle;
void USART0_IRQHandler(void)
{
    /* 总线空闲产生idle中断 */
    if (RESET != usart_interrupt_flag_get(USART0, USART_INT_FLAG_IDLE)) {
        usart_interrupt_flag_clear(USART0, USART_INT_FLAG_IDLE);
        usart0_idle_irq_count++;
        BaseType_t woken = pdFALSE;

        if (rx_task_handle != NULL) {
            vTaskNotifyGiveFromISR(rx_task_handle, &woken);
            portYIELD_FROM_ISR(woken);
        }
    }
}
