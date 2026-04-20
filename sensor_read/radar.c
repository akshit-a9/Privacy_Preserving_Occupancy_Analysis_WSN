#include "stm32f4xx.h"
#include "radar.h"

void Radar_Init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;

    /* PA8 → digital input with pull-down (OT1 is active-high push-pull). */
    GPIOA->MODER &= ~(3U << (8*2));
    GPIOA->PUPDR &= ~(3U << (8*2));
    GPIOA->PUPDR |=  (2U << (8*2));

    /* PA9 (TX), PA10 (RX) → AF7 (USART1). */
    GPIOA->MODER &= ~((3U << (9*2)) | (3U << (10*2)));
    GPIOA->MODER |=  ((2U << (9*2)) | (2U << (10*2)));
    GPIOA->AFR[1] &= ~((0xFU << ((9-8)*4)) | (0xFU << ((10-8)*4)));
    GPIOA->AFR[1] |=  ((7U << ((9-8)*4)) | (7U << ((10-8)*4)));

    /* USART1 on APB2 @ 16 MHz HSI. 115200 → BRR = 0x8B (see uart.c). */
    USART1->BRR = 0x8B;
    USART1->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;
}

int Radar_Presence(void)
{
    return (GPIOA->IDR >> 8) & 1U;
}

int Radar_UART_Available(void)
{
    return (USART1->SR & USART_SR_RXNE) ? 1 : 0;
}

uint8_t Radar_UART_ReadByte(void)
{
    while (!(USART1->SR & USART_SR_RXNE));
    return (uint8_t)(USART1->DR & 0xFFU);
}
