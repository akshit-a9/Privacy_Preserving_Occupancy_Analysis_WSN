#ifndef RADAR_H
#define RADAR_H

#include <stdint.h>

/* HLK-LD2420 mmWave radar driver.
 *   OT1  → PA8  (GPIO input: 1 = presence, 0 = clear)
 *   TX   → PA10 (USART1 RX, module → MCU)
 *   RX   → PA9  (USART1 TX, MCU → module, for commands)
 *   UART: 115200 8N1 (default LD2420 firmware).
 *
 * LD2420 in normal "reporting" mode streams ASCII frames like:
 *   "ON\r\n"  or  "OFF\r\n"
 * and in extended/debug mode emits binary frames bracketed by
 * 0xF4 0xF3 0xF2 0xF1 ... 0xF8 0xF7 0xF6 0xF5. */

void Radar_Init(void);
int  Radar_Presence(void);             /* OT1 pin state, 0/1 */
int  Radar_UART_Available(void);       /* non-zero if a byte is waiting */
uint8_t Radar_UART_ReadByte(void);     /* blocks until a byte arrives */

#endif
