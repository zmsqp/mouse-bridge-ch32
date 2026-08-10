#ifndef __CH32V20X_USBFS_DEVICE_H_
#define __CH32V20X_USBFS_DEVICE_H_

#include "debug.h"
#include "string.h"
#include "ch32v20x_usb.h"
#include "usbfs_desc.h"

#define DEF_UEP_IN                    0x80
#define DEF_UEP_OUT                   0x00
#define DEF_UEP0                      0x00
#define DEF_UEP1                      0x01
#define DEF_UEP2                      0x02
#define DEF_UEP3                      0x03
#define DEF_UEP4                      0x04
#define DEF_UEP5                      0x05
#define DEF_UEP6                      0x06
#define DEF_UEP7                      0x07
#define DEF_UEP_NUM                   8

#define USBFSD_UEP_MOD_BASE           0x5000000C
#define USBFSD_UEP_DMA_BASE           0x50000010
#define USBFSD_UEP_LEN_BASE           0x50000030
#define USBFSD_UEP_CTL_BASE           0x50000032
#define USBFSD_UEP_RX_EN              0x08
#define USBFSD_UEP_TX_EN              0x04
#define USBFSD_UEP_BUF_MOD            0x01
#define DEF_UEP_DMA_LOAD              0
#define DEF_UEP_CPY_LOAD              1
#define USBFSD_UEP_MOD(n)             (*((volatile uint8_t *)(USBFSD_UEP_MOD_BASE+n)))
#define USBFSD_UEP_TX_CTRL(n)         (*((volatile uint8_t *)(USBFSD_UEP_CTL_BASE+n*0x04)))
#define USBFSD_UEP_RX_CTRL(n)         (*((volatile uint8_t *)(USBFSD_UEP_CTL_BASE+n*0x04+1)))
#define USBFSD_UEP_DMA(n)             (*((volatile uint32_t *)(USBFSD_UEP_DMA_BASE+n*0x04)))
#define USBFSD_UEP_BUF(n)             ((uint8_t *)(*((volatile uint32_t *)(USBFSD_UEP_DMA_BASE+n*0x04)))+0x20000000)
#define USBFSD_UEP_TLEN(n)            (*((volatile uint16_t *)(USBFSD_UEP_LEN_BASE+n*0x04)))

#define pUSBFS_SetupReqPak            ((PUSB_SETUP_REQ)USBFS_EP0_Buf)

extern const uint8_t *pUSBFS_Descr;

extern volatile uint8_t  USBFS_SetupReqCode;
extern volatile uint8_t  USBFS_SetupReqType;
extern volatile uint16_t USBFS_SetupReqValue;
extern volatile uint16_t USBFS_SetupReqIndex;
extern volatile uint16_t USBFS_SetupReqLen;

extern volatile uint8_t  USBFS_DevConfig;
extern volatile uint8_t  USBFS_DevAddr;
extern volatile uint8_t  USBFS_DevSleepStatus;
extern volatile uint8_t  USBFS_DevEnumStatus;

extern __attribute__ ((aligned(4))) uint8_t USBFS_EP0_Buf[];
extern __attribute__ ((aligned(4))) uint8_t USBFS_EP2_Buf[];
extern volatile uint8_t USBFS_Endp_Busy[];

extern void USBFS_Device_Init(FunctionalState sta);
extern void USBFS_Device_Endp_Init(void);
extern uint8_t USBFS_Endp_DataUp(uint8_t endp, uint8_t *pbuf, uint16_t len, uint8_t mod);

#endif
