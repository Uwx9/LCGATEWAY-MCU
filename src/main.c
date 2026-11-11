/*!
    \file    main.c
    \brief   USART send and receive

    \version 2026-01-01, V2.6.0, firmware for GD32F3x0
*/

/*
    Copyright (c) 2026, GigaDevice Semiconductor Inc.

    Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

    1. Redistributions of source code must retain the above copyright notice, this
       list of conditions and the following disclaimer.
    2. Redistributions in binary form must reproduce the above copyright notice,
       this list of conditions and the following disclaimer in the documentation
       and/or other materials provided with the distribution.
    3. Neither the name of the copyright holder nor the names of its contributors
       may be used to endorse or promote products derived from this software without
       specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
OF SUCH DAMAGE.
*/

#include "gd32f3x0.h"
#include <stdio.h>

#define USART_PERIPH      USART0
#define USART_CLK         RCU_USART0
#define USART_GPIO_PORT   GPIOA
#define USART_GPIO_CLK    RCU_GPIOA
#define USART_GPIO_AF     GPIO_AF_1
#define USART_TX_PIN      GPIO_PIN_9
#define USART_RX_PIN      GPIO_PIN_10
#define USART_BAUDRATE    115200U

static void usart_config(void);
static void usart_send_byte(uint8_t data);
static void usart_send_string(const char *str);
static uint8_t usart_receive_byte(uint8_t *data);

/* printf 重定向到串口 */
int __io_putchar(int ch)
{
    usart_send_byte((uint8_t)ch);
    return ch;
}

static void usart_config(void)
{
    rcu_periph_clock_enable(USART_GPIO_CLK);
    rcu_periph_clock_enable(USART_CLK);

    /* PA9 -> USART0_TX, PA10 -> USART0_RX */
    gpio_af_set(USART_GPIO_PORT, USART_GPIO_AF, USART_TX_PIN | USART_RX_PIN);
    gpio_mode_set(USART_GPIO_PORT, GPIO_MODE_AF, GPIO_PUPD_PULLUP,
                  USART_TX_PIN | USART_RX_PIN);
    gpio_output_options_set(USART_GPIO_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ,
                            USART_TX_PIN);

    usart_deinit(USART_PERIPH);
    usart_word_length_set(USART_PERIPH, USART_WL_8BIT);
    usart_stop_bit_set(USART_PERIPH, USART_STB_1BIT);
    usart_parity_config(USART_PERIPH, USART_PM_NONE);
    usart_baudrate_set(USART_PERIPH, USART_BAUDRATE);
    usart_receive_config(USART_PERIPH, USART_RECEIVE_ENABLE);
    usart_transmit_config(USART_PERIPH, USART_TRANSMIT_ENABLE);
    usart_enable(USART_PERIPH);
}

static void usart_send_byte(uint8_t data)
{
    while(RESET == usart_flag_get(USART_PERIPH, USART_FLAG_TBE)) {
    }
    usart_data_transmit(USART_PERIPH, data);
}

static void usart_send_string(const char *str)
{
    while('\0' != *str) {
        usart_send_byte((uint8_t)*str++);
    }
    /* 等最后一个字节移出移位寄存器 */
    while(RESET == usart_flag_get(USART_PERIPH, USART_FLAG_TC)) {
    }
}

static uint8_t usart_receive_byte(uint8_t *data)
{
    if(RESET == usart_flag_get(USART_PERIPH, USART_FLAG_RBNE)) {
        return 0U;
    }

    *data = (uint8_t)usart_data_receive(USART_PERIPH);
    return 1U;
}

int main(void)
{
    uint8_t data;

    usart_config();

    usart_send_string("\r\nGD32F310 USART0 ready, 115200 8N1\r\n");

    while(1) {
        /* 收到什么就回什么 */
        if(usart_receive_byte(&data)) {
            usart_send_byte(data);
        }
    }
}
