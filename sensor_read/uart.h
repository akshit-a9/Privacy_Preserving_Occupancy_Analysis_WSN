#ifndef UART_H
#define UART_H

#include <stdint.h>

/* USART2 on PA2 (TX) / PA3 (RX) → ST-LINK VCP on Nucleo-F446RE.
 * Default: 115200 8N1 @ HSI 16 MHz. */
void UART2_Init(void);
void UART_SendByte(uint8_t data);
void UART_SendArray(uint8_t *buf, uint16_t len);
void UART_SendString(const char *str);

#endif
