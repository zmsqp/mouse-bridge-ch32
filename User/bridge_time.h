#ifndef __BRIDGE_TIME_H
#define __BRIDGE_TIME_H

#include <stdint.h>

void BridgeTime_Init(void);
void BridgeTime_Poll(void);
uint32_t BridgeTime_GetMs(void);

#endif /* __BRIDGE_TIME_H */
