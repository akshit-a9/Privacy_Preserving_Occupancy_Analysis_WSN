#ifndef ADC_H
#define ADC_H

#include <stdint.h>

/* ADC1 single-conversion driver.
 * Channel 0 = PA0 (Acoustic, Arduino A0)
 * Channel 1 = PA1 (Vibration, Arduino A1) */
void ADC1_Init(void);
uint16_t ADC1_Read(uint8_t channel);

#endif
