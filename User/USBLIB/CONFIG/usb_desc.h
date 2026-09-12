#ifndef __USB_DESC_H
#define __USB_DESC_H

#ifdef __cplusplus
 extern "C" {
#endif

#include "ch32v20x.h"

#define USB_DEVICE_DESCRIPTOR_TYPE              0x01
#define USB_CONFIGURATION_DESCRIPTOR_TYPE       0x02
#define USB_STRING_DESCRIPTOR_TYPE              0x03
#define USB_INTERFACE_DESCRIPTOR_TYPE           0x04
#define USB_ENDPOINT_DESCRIPTOR_TYPE            0x05

#define DEF_USBD_UEP0_SIZE        64
#define DEF_ENDP_SIZE_KB            8
#define DEF_ENDP_SIZE_MS           64
#define DEF_ENDP_SIZE_INJECT        3
#define DEF_ENDP_SIZE_CFG            8

#define USBD_SIZE_DEVICE_DESC        18
#define USBD_SIZE_CONFIG_DESC        84
#define USBD_SIZE_REPORT_DESC_KB     62
#define USBD_SIZE_REPORT_DESC_MS     52
#define USBD_SIZE_REPORT_DESC_INJECT 28
#define USBD_SIZE_REPORT_DESC_MAX   512
#define USBD_SIZE_STRING_LANGID      4
#define USBD_SIZE_STRING_VENDOR      14
#define USBD_SIZE_STRING_PRODUCT     20
#define USBD_SIZE_STRING_SERIAL      22

#define STANDARD_ENDPOINT_DESC_SIZE             0x09

extern const uint8_t USBD_DeviceDescriptor[USBD_SIZE_DEVICE_DESC];
extern uint8_t USBD_ConfigDescriptor[USBD_SIZE_CONFIG_DESC];

extern const uint8_t USBD_StringLangID[USBD_SIZE_STRING_LANGID];
extern const uint8_t USBD_StringVendor[USBD_SIZE_STRING_VENDOR];
extern const uint8_t USBD_StringProduct[USBD_SIZE_STRING_PRODUCT];
extern uint8_t USBD_StringSerial[USBD_SIZE_STRING_SERIAL];
extern uint8_t USBD_MouseRepDesc[USBD_SIZE_REPORT_DESC_MAX];
extern uint16_t USBD_MouseRepDesc_Len;
extern const uint8_t USBD_InjectMouseRepDesc[USBD_SIZE_REPORT_DESC_INJECT];

void USBD_LoadDefaultReportDescriptor(void);
void USBD_LoadPassthroughReportDescriptor(const uint8_t *desc, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* __USB_DESC_H */
