#include "stm32f4xx.h"
#include "adc.h"

void ADC1_Init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;

    /* PA0, PA1 → analog mode (MODER = 11) */
    GPIOA->MODER |= (3U << (0*2)) | (3U << (1*2));

    ADC1->CR1 = 0;                          /* 12-bit, no scan */
    ADC1->CR2 = 0;                          /* single conversion, right-align */
    ADC1->SMPR2 = (7U << (0*3)) | (7U << (1*3));  /* 480 cycles for CH0, CH1 */
    ADC1->SQR1 = 0;                         /* L = 0 → 1 conversion */

    ADC1->CR2 |= ADC_CR2_ADON;
}

uint16_t ADC1_Read(uint8_t channel)
{
    ADC1->SQR3 = channel & 0x1F;
    ADC1->CR2 |= ADC_CR2_SWSTART;
    while (!(ADC1->SR & ADC_SR_EOC));
    return (uint16_t)(ADC1->DR & 0xFFFF);
}
