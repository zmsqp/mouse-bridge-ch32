#ifndef __BRIDGE_UART_CMD_H
#define __BRIDGE_UART_CMD_H

#include <stdint.h>

void BridgeUart_Init(uint32_t baudrate);
void BridgeUart_Poll(void);

#endif /* __BRIDGE_UART_CMD_H */
