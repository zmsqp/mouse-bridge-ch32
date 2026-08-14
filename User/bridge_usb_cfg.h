#ifndef __BRIDGE_USB_CFG_H
#define __BRIDGE_USB_CFG_H

#include <stdint.h>

/* HID Feature Report（无显式 Report ID，Windows 复合设备用 ID 0） */
#define BRIDGE_USB_RPT_ID     0x00U
#define BRIDGE_USB_RPT_SIZE   32U

#define BRIDGE_USB_CMD_NOP    0x00U
#define BRIDGE_USB_CMD_GET    0x01U
#define BRIDGE_USB_CMD_SET    0x02U
#define BRIDGE_USB_CMD_SAVE   0x03U
#define BRIDGE_USB_CMD_MON    0x04U

#define BRIDGE_USB_ACK_OK     0xA5U

void BridgeUsbCfg_Init(void);
void BridgeUsbCfg_Poll(void);
void BridgeUsbCfg_OnControlOutDone(void);
void BridgeUsbCfg_FillFeatureReport(uint8_t *buf, uint16_t maxlen);

#endif /* __BRIDGE_USB_CFG_H */
