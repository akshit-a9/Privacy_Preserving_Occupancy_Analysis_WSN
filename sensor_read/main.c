#include "stm32f4xx.h"
#include "uart.h"
#include "delay.h"
#include "adc.h"
#include "radar.h"
#include "occupancy.h"

#define SAMPLE_PERIOD_MS    100

static void uint_to_str(uint32_t val, char *buf)
{
    char tmp[11];
    int  i = 0;
    int  j;

    if (val == 0) { tmp[i++] = '0'; }
    while (val > 0) { tmp[i++] = (char)('0' + (val % 10)); val /= 10; }
    for (j = 0; j < i; j++) buf[j] = tmp[i - 1 - j];
    buf[i] = '\0';
}

static void int_to_str(int val, char *buf)
{
    if (val < 0) { *buf++ = '-'; val = -val; }
    uint_to_str((uint32_t)val, buf);
}

int main(void)
{
    uint16_t  acoustic, vibration;
    int       presence, range;
    char      num[12];
    OccResult occ;

    SystemInit();
    SysTick_Init();
    UART2_Init();
    ADC1_Init();
    Radar_Init();
    Occupancy_Init();

    /* Give the LD2420 ~1 s to boot and start streaming. */
    delay_ms(1000);
    UART_SendString("# DNES sensor node online\r\n");
    UART_SendString("# CSV: t_ms,acoustic,vibration,presence,range,ci_milli,class\r\n");

    while (1)
    {
        acoustic  = ADC1_Read(0);  /* PA0 */
        vibration = ADC1_Read(1);  /* PA1 */
        range     = Radar_Range();     /* drains USART1 and parses the stream */
        presence  = Radar_Presence();  /* uses state populated by the drain   */

        occ = Occupancy_Update(acoustic, vibration, presence);

        uint_to_str(ticks, num);     UART_SendString(num); UART_SendByte(',');
        uint_to_str(acoustic, num);  UART_SendString(num); UART_SendByte(',');
        uint_to_str(vibration, num); UART_SendString(num); UART_SendByte(',');
        uint_to_str((uint32_t)presence, num); UART_SendString(num); UART_SendByte(',');
        int_to_str(range, num);      UART_SendString(num); UART_SendByte(',');
        uint_to_str((uint32_t)occ.ci_milli, num); UART_SendString(num); UART_SendByte(',');
        UART_SendByte((uint8_t)Occupancy_ClassChar(occ.cls));

        UART_SendString("\r\n");
        delay_ms(SAMPLE_PERIOD_MS);
    }
}
