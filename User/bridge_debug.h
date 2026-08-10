#ifndef __BRIDGE_DEBUG_H
#define __BRIDGE_DEBUG_H

#include "usb_bridge_config.h"
#include <stdint.h>

/* ISR-safe event codes (printed later from main loop) */
#define BRIDGE_EV_RESET          1
#define BRIDGE_EV_ERR            2
#define BRIDGE_EV_CTR_SETUP      3
#define BRIDGE_EV_CTR_IN0        4
#define BRIDGE_EV_CTR_OUT0       5
#define BRIDGE_EV_SUSP           6
#define BRIDGE_EV_SOF            7
#define BRIDGE_EV_DOVR           8
#define BRIDGE_EV_SETUP          9
#define BRIDGE_EV_GET_DEV_DESC  10
#define BRIDGE_EV_STATE         11
#define BRIDGE_EV_WKUP          12

#if BRIDGE_DEBUG_UART

void BridgeDebug_Init(void);
void BridgeDebug_LogUsbInit(void);
void BridgeDebug_IsrPush(uint8_t ev, uint8_t arg0, uint16_t arg1);
void BridgeDebug_Flush(void);
void BridgeDebug_Tick(void);
void BridgeDebug_LogHostEnum(uint8_t ok, uint8_t err_code);
void BridgeDebug_LogMouseReady(uint8_t intf, uint16_t ep_size, uint8_t report_id);
void BridgeDebug_LogForward(const uint8_t *data, uint16_t len, uint8_t sent);
void BridgeDebug_LogPcHostReady(void);

void RESET_Callback(void);
void ERR_Callback(void);
void SUSP_Callback(void);
void SOF_Callback(void);
void DOVR_Callback(void);
void WKUP_Callback(void);

#else

#define BridgeDebug_Init()              do {} while(0)
#define BridgeDebug_LogUsbInit()        do {} while(0)
#define BridgeDebug_IsrPush(a,b,c)      do {} while(0)
#define BridgeDebug_Flush()             do {} while(0)
#define BridgeDebug_Tick()              do {} while(0)
#define BridgeDebug_LogHostEnum(a,b)    do {} while(0)
#define BridgeDebug_LogMouseReady(a,b,c) do {} while(0)
#define BridgeDebug_LogForward(a,b,c)   do {} while(0)
#define BridgeDebug_LogPcHostReady()    do {} while(0)
#define RESET_Callback()                do {} while(0)
#define ERR_Callback()                  do {} while(0)
#define SUSP_Callback()                 do {} while(0)
#define SOF_Callback()                  do {} while(0)
#define DOVR_Callback()                 do {} while(0)
#define WKUP_Callback()                 do {} while(0)

#endif

#endif /* __BRIDGE_DEBUG_H */
