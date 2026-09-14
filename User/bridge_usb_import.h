#ifndef __BRIDGE_USB_IMPORT_H
#define __BRIDGE_USB_IMPORT_H

#include <stdint.h>

/*
 * Vendor HID Feature Report carried by interface 2 of the same composite
 * USB device.  HID user-space APIs add a leading zero report-id byte; the
 * USB control transfer itself carries the 63-byte payload below.
 */
#define BRIDGE_USB_IMPORT_REPORT_ID          0x00U
#define BRIDGE_USB_IMPORT_PAYLOAD_SIZE       63U
#define BRIDGE_USB_IMPORT_HOST_REPORT_SIZE   64U
#define BRIDGE_USB_IMPORT_DATA_SIZE          48U
#define BRIDGE_USB_IMPORT_REPORT_DESC_SIZE   21U

#define BRIDGE_USB_IMPORT_CMD_PING            0x01U
#define BRIDGE_USB_IMPORT_CMD_GET_STATUS      0x02U
#define BRIDGE_USB_IMPORT_CMD_BEGIN           0x10U
#define BRIDGE_USB_IMPORT_CMD_DATA            0x11U
#define BRIDGE_USB_IMPORT_CMD_COMMIT          0x12U
#define BRIDGE_USB_IMPORT_CMD_ABORT           0x13U
#define BRIDGE_USB_IMPORT_CMD_SET_ACTIVE      0x20U
#define BRIDGE_USB_IMPORT_CMD_SAVE_ACTIVE     0x21U
#define BRIDGE_USB_IMPORT_CMD_MONITOR         0x22U
#define BRIDGE_USB_IMPORT_CMD_ENTER_IAP       0x30U

#define BRIDGE_USB_IMPORT_STATUS_OK           0x00U
#define BRIDGE_USB_IMPORT_STATUS_READY        0x01U
#define BRIDGE_USB_IMPORT_STATUS_PROFILE_OK   0x02U
#define BRIDGE_USB_IMPORT_STATUS_FLASH_OK     0x03U
#define BRIDGE_USB_IMPORT_STATUS_IAP_READY    0x04U
#define BRIDGE_USB_IMPORT_ERR_PACKET          0x80U
#define BRIDGE_USB_IMPORT_ERR_CRC             0x81U
#define BRIDGE_USB_IMPORT_ERR_SESSION         0x82U
#define BRIDGE_USB_IMPORT_ERR_SEQUENCE        0x83U
#define BRIDGE_USB_IMPORT_ERR_RANGE           0x84U
#define BRIDGE_USB_IMPORT_ERR_PROFILE         0x85U
#define BRIDGE_USB_IMPORT_ERR_INCOMPLETE      0x86U
#define BRIDGE_USB_IMPORT_ERR_FLASH           0x87U
#define BRIDGE_USB_IMPORT_ERR_TIMEOUT         0x88U
#define BRIDGE_USB_IMPORT_ERR_BUSY            0x89U
#define BRIDGE_USB_IMPORT_ERR_COMMAND         0x8AU

extern const uint8_t BridgeUsbImport_ReportDescriptor[BRIDGE_USB_IMPORT_REPORT_DESC_SIZE];

void BridgeUsbImport_Init(void);
void BridgeUsbImport_Poll(void);
void BridgeUsbImport_QueueFeatureReport(const uint8_t *buf, uint16_t len);
void BridgeUsbImport_FillFeatureReport(uint8_t *buf, uint16_t len);

#endif /* __BRIDGE_USB_IMPORT_H */
