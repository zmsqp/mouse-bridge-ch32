#ifndef __MOUSE_BRIDGE_H
#define __MOUSE_BRIDGE_H

#include "ch32v20x.h"

#define MOUSE_BRIDGE_REPORT_DESC_MAX   512
#define MOUSE_BRIDGE_REPORT_MAX          64

typedef struct
{
    uint8_t  enabled;
    uint8_t  hotkey_active;
    uint8_t  reserved[2];
    int16_t  modify_dx;
    int16_t  modify_dy;
} MouseBridgeConfig;

void MouseBridge_Init(void);
void MouseBridge_CloneDeviceDescriptor(const uint8_t *src);
void MouseBridge_SetReportDescriptor(const uint8_t *src, uint16_t len);
void MouseBridge_SetEndpointParams(uint16_t max_packet, uint8_t interval);
void MouseBridge_BuildConfigDescriptor(void);
void MouseBridge_OnHostReady(void);
void MouseBridge_ForwardReport(const uint8_t *data, uint16_t len);
void MouseBridge_Poll(void);
MouseBridgeConfig *MouseBridge_GetConfig(void);

#endif
