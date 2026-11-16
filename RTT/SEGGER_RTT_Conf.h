/*********************************************************************
*                   (c) SEGGER Microcontroller GmbH                  *
*                        The Embedded Experts                        *
*                           www.segger.com                           *
**********************************************************************
*                                                                    *
*        SEGGER RTT * Real Time Transfer for embedded targets        *
*                  https://github.com/SEGGERMicro/RTT                *
*                                                                    *
**********************************************************************

--------------------------- END-OF-HEADER------------------------------
Purpose : User configuration file for RTT.
          For available configuration,
          refer to SEGGER_RTT_ConfDefaults.h.

          本文件是官方提供的"用户覆盖区"，默认是空的，
          所有默认值在 SEGGER_RTT_ConfDefaults.h 里。
          下面几行是为 8KB RAM 的 GD32F3x0 压出来的配置。
----------------------------------------------------------------------
*/

#ifndef SEGGER_RTT_CONF_H
#define SEGGER_RTT_CONF_H

/*********************************************************************
*
*       Defines, configurable
*
**********************************************************************
*/

/* 上行缓冲：256 字节，必须 <= 2 的幂且 > 0 */
#define BUFFER_SIZE_UP                       256

/* 下行缓冲：不用 PC->MCU 传参也留着，最小 16
 * （SEGGER_RTT_ConfDefaults.h 里 _DoInit 无条件写 aDown[0]，
 *   MAX_NUM_DOWN_BUFFERS 不能设成 0） */
#define BUFFER_SIZE_DOWN                     16

/* 通道数：各 1 条，控制块从 168 字节降到 72 字节 */
#define SEGGER_RTT_MAX_NUM_UP_BUFFERS        1
#define SEGGER_RTT_MAX_NUM_DOWN_BUFFERS      1

/* SEGGER_RTT_printf 内部格式化缓冲，默认 64，够用 */
#define SEGGER_RTT_PRINTF_BUFFER_SIZE        64u

#endif
/*************************** End of file ****************************/
