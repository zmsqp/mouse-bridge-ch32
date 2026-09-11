#ifndef __LED_INDICATOR_H
#define __LED_INDICATOR_H

#include "ch32v20x.h"

typedef enum
{
    LED_INDICATOR_OFF = 0,
    LED_INDICATOR_ON,
    LED_INDICATOR_SLOW,
    LED_INDICATOR_FAST
} LedIndicatorMode;

/* PB9 高电平点亮。熄灭=未开启，常亮=已开启，快闪=正在压枪，慢闪=切枪确认。 */
void LED_Indicator_Init(void);
void LED_Indicator_SetMode(LedIndicatorMode mode);
void LED_Indicator_PulseSelection(void);

#endif
