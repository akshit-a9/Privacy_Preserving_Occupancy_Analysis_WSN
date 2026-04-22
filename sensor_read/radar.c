#include "stm32f4xx.h"
#include "radar.h"

static volatile int radar_presence_state = 0;
static volatile int radar_range_value    = -1;
static char         line_buf[24];
static unsigned     line_len = 0;

void Radar_Init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;

    /* PA10 → AF7 (USART1_RX), fed by LD2420 OT1. */
    GPIOA->MODER  &= ~(3U   << (10*2));
    GPIOA->MODER  |=  (2U   << (10*2));
    GPIOA->AFR[1] &= ~(0xFU << ((10-8)*4));
    GPIOA->AFR[1] |=  (7U   << ((10-8)*4));

    /* USART1 on APB2 @ 16 MHz HSI. 115200 → BRR = 0x8B. RX only. */
    USART1->BRR = 0x8B;
    USART1->CR1 = USART_CR1_RE | USART_CR1_UE;
}

static void parse_line(void)
{
    /* "OFF" → target lost. */
    if (line_len >= 3 && line_buf[0]=='O' && line_buf[1]=='F' && line_buf[2]=='F')
    {
        radar_presence_state = 0;
        radar_range_value    = -1;
        return;
    }
    /* "ON" → target present (no distance info on this line). */
    if (line_len >= 2 && line_buf[0]=='O' && line_buf[1]=='N')
    {
        radar_presence_state = 1;
        return;
    }
    /* "Range <N>" → distance in module-native units. Also implies presence. */
    if (line_len >= 7 &&
        line_buf[0]=='R' && line_buf[1]=='a' && line_buf[2]=='n' &&
        line_buf[3]=='g' && line_buf[4]=='e' && line_buf[5]==' ')
    {
        int      val = 0;
        unsigned i   = 6;
        int      any = 0;
        while (i < line_len && line_buf[i] >= '0' && line_buf[i] <= '9')
        {
            val = val * 10 + (line_buf[i] - '0');
            any = 1;
            i++;
        }
        if (any)
        {
            radar_range_value    = val;
            radar_presence_state = 1;
        }
    }
}

static void parse_byte(uint8_t b)
{
    if (b == '\r' || b == '\n')
    {
        if (line_len > 0) parse_line();
        line_len = 0;
    }
    else if (line_len < (sizeof line_buf - 1))
    {
        line_buf[line_len++] = (char)b;
    }
    else
    {
        line_len = 0;  /* overflow → resync on next newline */
    }
}

static void drain(void)
{
    while (USART1->SR & USART_SR_RXNE)
        parse_byte((uint8_t)(USART1->DR & 0xFFU));
}

int Radar_Presence(void)
{
    drain();
    return radar_presence_state;
}

int Radar_Range(void)
{
    drain();
    return radar_range_value;
}
