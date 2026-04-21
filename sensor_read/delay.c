#include "stm32f4xx.h"
#include "delay.h"

volatile uint32_t ticks;

void SysTick_Init(void)
{
    SysTick_Config(SystemCoreClock / 1000);
}

void SysTick_Handler(void)
{
    ticks++;
}

void delay_ms(uint32_t ms)
{
    uint32_t start = ticks;
    while ((ticks - start) < ms);
}
