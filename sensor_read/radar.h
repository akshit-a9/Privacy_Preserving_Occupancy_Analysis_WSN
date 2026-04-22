#ifndef RADAR_H
#define RADAR_H

#include <stdint.h>

/* HLK-LD2420 mmWave radar driver.
 *
 * OT1 is the module's only output and is a 115200 8N1 UART line.
 * In simple reporting mode it streams ASCII frames, e.g.:
 *     Range 65
 *     ON
 *     Range 64
 *     ON
 *     OFF
 * We wire it into USART1_RX (PA10) and parse for range + presence.
 *
 * Wiring:
 *   LD2420 OT1 → PA10  (USART1_RX, AF7)
 *   LD2420 VCC → 3V3
 *   LD2420 GND → GND
 */

void Radar_Init(void);
int  Radar_Presence(void);   /* 1 = target, 0 = clear */
int  Radar_Range(void);      /* latest "Range N" value; -1 when no target */

#endif
