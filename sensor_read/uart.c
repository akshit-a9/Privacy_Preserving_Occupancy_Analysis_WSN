#include "stm32f4xx.h"
#include "uart.h"

/* BRR for 115200 baud @ 16 MHz APB1 (HSI, no PLL):
 *   USARTDIV = 16e6 / (16 * 115200) = 8.6805
 *   Mantissa = 8, Fraction = round(0.6805 * 16) = 11
 *   BRR = (8 << 4) | 11 = 0x8B
 */
void UART2_Init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    RCC->APB1ENR |= RCC_APB1ENR_USART2EN;

    GPIOA->MODER &= ~((3U << (2*2)) | (3U << (3*2)));
    GPIOA->MODER |=  ((2U << (2*2)) | (2U << (3*2)));   /* AF mode */
    GPIOA->AFR[0] &= ~((0xFU << (2*4)) | (0xFU << (3*4)));
    GPIOA->AFR[0] |=  ((7U << (2*4)) | (7U << (3*4)));  /* AF7 = USART2 */

    USART2->BRR = 0x8B;
    USART2->CR1 = USART_CR1_TE | USART_CR1_UE;
}

void UART_SendByte(uint8_t data)
{
    while (!(USART2->SR & USART_SR_TXE));
    USART2->DR = data;
}

void UART_SendArray(uint8_t *buf, uint16_t len)
{
    uint16_t i;
    for (i = 0; i < len; i++)
        UART_SendByte(buf[i]);
}

void UART_SendString(const char *str)
{
    while (*str)
    {
        UART_SendByte((uint8_t)*str);
        str++;
    }
}
