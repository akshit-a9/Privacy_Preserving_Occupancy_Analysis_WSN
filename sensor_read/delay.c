#include "stm32f4xx.h"
#include "delay.h"

volatile uint32_t ticks;

void SysTick_Init(void)
{
    SystemCoreClockUpdate();
    if (SysTick_Config(SystemCoreClock / 1000))
    {
        while (1);
    }
    __enable_irq();
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
