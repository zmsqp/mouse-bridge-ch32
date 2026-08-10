#ifndef __USB_BRIDGE_CONFIG_H
#define __USB_BRIDGE_CONFIG_H

/*
 * PC 侧 USB 物理连接选择：
 * USB_PC_PORT_USBFS — P6(PB6/PB7)，WCH-Link 集线器透传口（Hub Port 2）
 * USB_PC_PORT_USBD  — P7/P8(PA11/PA12)，需单独 USB 线接 PC
 */
#define USB_PC_PORT_USBFS   1
#define USB_PC_PORT_USBD    2

/*
 * PC 侧：PA11/PA12 (USBD/USB3)
 *   PA11 = D- (USBDM)，PA12 = D+ (USBDP) — 见 CH32V203 数据手册
 * 鼠标接收器：PB6/PB7 (USBFS Host)
 *   PB6 = D- (USBFS_DM)，PB7 = D+ (USBFS_DP)
 */
#define USB_PC_PORT         USB_PC_PORT_USBD

/*
 * 串口调试（USB-TTL 模块）：
 *   默认 USART1 — PA9 = TX（接模块 RX），GND 共地，115200 8N1
 *   可选 USART2 — PA2 = TX
 *   可选 USART3 — PB10 = TX（改 Debug/debug.h 里 DEBUG 宏）
 */
#define BRIDGE_DEBUG_UART   1
#define BRIDGE_DEBUG_BAUD   115200

#endif
