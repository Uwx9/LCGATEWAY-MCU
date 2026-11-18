#include "DIDO.h"
#include "gd32f3x0.h"
#include <stddef.h>
#include <stdint.h>

#define DO_OK           0
#define DO_ERR_STATUS   1
#define DO_ERR_ID       2

typedef struct {
    uint32_t port;
    uint32_t pin;
    rcu_periph_enum clock;
} do_pin_t;

/*
 * DO 编号到 GPIO 的映射表，数组下标 0 对应 DO1。
 *
 * 当前按 DO1~DO16 = PB0~PB15 占用 GPIOB（LQFP48 上全部可用）。
 * 实际接线确定后按原理图改这里即可。
 *
 * LQFP48 上 PC0~PC12 没有引出，只有 PC13/PC14/PC15 可用；
 * 已确认不能随意占用的引脚：
 *   PA9/PA10   USART0 TX/RX
 *   PA13/PA14  SWDIO/SWCLK
 *   PF0/PF1    HXTAL，84MHz PLL 依赖
 * 另外 PA11/PA12 预留给 USB。
 */
static const do_pin_t s_do_map[] = {
    {GPIOB, GPIO_PIN_0,  RCU_GPIOB},   /* DO1 */
    {GPIOB, GPIO_PIN_1,  RCU_GPIOB},   /* DO2 */
    {GPIOB, GPIO_PIN_2,  RCU_GPIOB},   /* DO3 */
    {GPIOB, GPIO_PIN_3,  RCU_GPIOB},   /* DO4 */
    {GPIOB, GPIO_PIN_4,  RCU_GPIOB},   /* DO5 */
    {GPIOB, GPIO_PIN_5,  RCU_GPIOB},   /* DO6 */
    {GPIOB, GPIO_PIN_6,  RCU_GPIOB},   /* DO7 */
    {GPIOB, GPIO_PIN_7,  RCU_GPIOB},   /* DO8 */
    {GPIOB, GPIO_PIN_8,  RCU_GPIOB},   /* DO9 */
    {GPIOB, GPIO_PIN_9,  RCU_GPIOB},   /* DO10 */
    {GPIOB, GPIO_PIN_10, RCU_GPIOB},   /* DO11 */
    {GPIOB, GPIO_PIN_11, RCU_GPIOB},   /* DO12 */
    {GPIOB, GPIO_PIN_12, RCU_GPIOB},   /* DO13 */
    {GPIOB, GPIO_PIN_13, RCU_GPIOB},   /* DO14 */
    {GPIOB, GPIO_PIN_14, RCU_GPIOB},   /* DO15 */
    {GPIOB, GPIO_PIN_15, RCU_GPIOB},   /* DO16 */
};

#define DO_COUNT  ((uint8_t)(sizeof(s_do_map) / sizeof(s_do_map[0])))

/* 设置DO, 0表示成功, 非0表示失败 */
int DO_set(uint8_t DO_id, uint8_t status)
{
    const do_pin_t *map;

    if ((status != 0U) && (status != 1U)) {
        return DO_ERR_STATUS;
    }
    if ((DO_id == 0U) || (DO_id > DO_COUNT)) {
        return DO_ERR_ID;
    }

    map = &s_do_map[DO_id - 1U];

    rcu_periph_clock_enable(map->clock);
    gpio_mode_set(map->port, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, map->pin);
    gpio_output_options_set(map->port, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ, map->pin);

    if (status == 0U) {
        gpio_bit_reset(map->port, map->pin);
    } else {
        gpio_bit_set(map->port, map->pin);
    }

    return DO_OK;
}
