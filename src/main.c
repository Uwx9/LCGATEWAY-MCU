#include "FreeRTOS.h"
#include "gd32f3x0.h"
#include "gd32f3x0_libopt.h"
#include "task.h"

#define LED_TASK_STACK (128U) /* 单位：字 */
#define HB_TASK_STACK (128U)
#define LED_TASK_PRIO (1U)
#define HB_TASK_PRIO (2U)

static void led_task(void *pvParameters);
static void heartbeat_task(void *pvParameters);

/* PC10 -> PC11 -> PC12 轮流亮，每 500 ms 换一个 */
static void led_task(void *pvParameters) {
  (void)pvParameters;

  for (;;) {
    gpio_bit_write(GPIOC, GPIO_PIN_10, SET);
    gpio_bit_write(GPIOC, GPIO_PIN_12, RESET);
    vTaskDelay(pdMS_TO_TICKS(500));

    gpio_bit_write(GPIOC, GPIO_PIN_11, SET);
    gpio_bit_write(GPIOC, GPIO_PIN_10, RESET);
    vTaskDelay(pdMS_TO_TICKS(500));

    gpio_bit_write(GPIOC, GPIO_PIN_12, SET);
    gpio_bit_write(GPIOC, GPIO_PIN_11, RESET);
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}

/* PC2 每 100 ms 翻转一次，作为心跳 */
static void heartbeat_task(void *pvParameters) {
  (void)pvParameters;

  for (;;) {
    gpio_bit_write(GPIOC, GPIO_PIN_2, SET);
    vTaskDelay(pdMS_TO_TICKS(100));

    gpio_bit_write(GPIOC, GPIO_PIN_2, RESET);
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

int main(void) {
  BaseType_t ret;

  /* FreeRTOS 要求所有优先级位都做抢占优先级 */
  nvic_priority_group_set(NVIC_PRIGROUP_PRE4_SUB0);

  rcu_periph_clock_enable(RCU_GPIOC);

  gpio_mode_set(GPIOC, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE,
                GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_2);
  gpio_output_options_set(GPIOC, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ,
                          GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_2);

  gpio_bit_reset(GPIOC, GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_2);

  ret = xTaskCreate(led_task, "LED", LED_TASK_STACK, NULL, LED_TASK_PRIO, NULL);
  if (ret != pdPASS) {
    gpio_bit_set(GPIOC, GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_2);
    for (;;) {
    }
  }

  ret = xTaskCreate(heartbeat_task, "HB", HB_TASK_STACK, NULL, HB_TASK_PRIO,
                    NULL);
  if (ret != pdPASS) {
    gpio_bit_set(GPIOC, GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_2);
    for (;;) {
    }
  }

  vTaskStartScheduler();

  /* 正常不会走到这里；走到这里说明调度器启动失败 */
  for (;;) {
  }
}
