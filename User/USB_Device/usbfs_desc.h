#ifndef __USBFS_DESC_H
#define __USBFS_DESC_H

#include "stdint.h"

#define DEF_FILE_VERSION             0x01
#define DEF_USB_VID                  0x1A86
#define DEF_USB_PID                  0xFE01
#define DEF_IC_PRG_VER               DEF_FILE_VERSION

#define DEF_USBD_UEP0_SIZE           64
#define DEF_USBD_FS_PACK_SIZE        64
#define DEF_USB_EP2_FS_SIZE          DEF_USBD_FS_PACK_SIZE

#define USBFS_SIZE_CONFIG_DESC       34
#define USBFS_SIZE_DEVICE_DESC       18
#define USBFS_SIZE_REPORT_DESC_MS    52

#define DEF_USBD_DEVICE_DESC_LEN     USBFS_SIZE_DEVICE_DESC
#define DEF_USBD_CONFIG_DESC_LEN     USBFS_SIZE_CONFIG_DESC

#define DEF_USBD_LANG_DESC_LEN       ((uint16_t)MyLangDescr[0])
#define DEF_USBD_MANU_DESC_LEN       ((uint16_t)MyManuInfo[0])
#define DEF_USBD_PROD_DESC_LEN       ((uint16_t)MyProdInfo[0])
#define DEF_USBD_SN_DESC_LEN         ((uint16_t)MySerNumInfo[0])

extern uint8_t MyDevDescr[];
extern uint8_t MyCfgDescr[];
extern uint8_t MyMouseRepDesc[];
extern uint16_t USBFS_MouseRepDesc_Len;
extern const uint8_t MyLangDescr[];
extern const uint8_t MyManuInfo[];
extern const uint8_t MyProdInfo[];
extern const uint8_t MySerNumInfo[];

void USBFS_LoadDefaultReportDescriptor(void);

#endif
