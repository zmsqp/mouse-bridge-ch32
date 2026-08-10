#ifndef __LED_INDICATOR_H
#define __LED_INDICATOR_H

#include "ch32v20x.h"

/* PB9 高电平点亮，约 1Hz 闪烁表示程序在运行 */
void LED_Indicator_Init(void);

#endif
