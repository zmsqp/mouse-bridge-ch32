#include "usb_bridge_config.h"
#include "mouse_bridge.h"
#include "led_indicator.h"
#include "bridge_debug.h"
#include "ch32v20x_conf.h"
#include "string.h"

#if (USB_PC_PORT == USB_PC_PORT_USBFS)
#include "ch32v20x_usbfs_device.h"
#include "ch32v20x_usbfs_host.h"
#else
#include "usb_lib.h"
#include "usb_desc.h"
#include "usb_pwr.h"
#include "usb_prop.h"
#include "hw_config.h"
#endif

#include "usb_host_config.h"

/*
 * PC 侧 USBD（固定引脚，软件无法对调 D+/D-）：
 *   PA11 = USBDM = D-
 *   PA12 = USBDP = D+
 * 接收器侧 USBFS Host：
 *   PB6 = USBFS_DM = D-
 *   PB7 = USBFS_DP = D+
 * 若必须对调 D+/D- 线才能枚举，说明 PCB/座子走线接反，不是固件问题。
 */

int main(void)
{
    static uint8_t host_started = 0;

    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_1);
    SystemCoreClockUpdate();
    Delay_Init();
    LED_Indicator_Init();
    BridgeDebug_Init();

    MouseBridge_Init();

#if (USB_PC_PORT == USB_PC_PORT_USBFS)
    USBFS_RCC_Init();
    Delay_Ms(100);
    USBFS_Device_Init(ENABLE);
#else
    fSuspendEnabled = TRUE;
    Set_USBConfig();
    USB_Init();
    USB_Interrupts_Config();
    BridgeDebug_LogUsbInit();
#endif

    while(1)
    {
        BridgeDebug_Flush();
#if DEF_USBFS_PORT_EN && (USB_PC_PORT == USB_PC_PORT_USBD)
        /* PC 枚举完成后再启 Host，避免与 USBD 冲突导致描述符请求失败 */
        if(!host_started && bDeviceState == CONFIGURED)
        {
            BridgeDebug_LogPcHostReady();
            TIM3_Init(9, SystemCoreClock / 10000 - 1);
            USBFS_RCC_Init();
            USBFS_Host_Init(ENABLE);
            memset(&RootHubDev.bStatus, 0, sizeof(ROOT_HUB_DEVICE));
            memset(&HostCtl[DEF_USBFS_PORT_INDEX * DEF_ONE_USB_SUP_DEV_TOTAL].InterfaceNum, 0,
                   DEF_ONE_USB_SUP_DEV_TOTAL * sizeof(HOST_CTL));
            host_started = 1;
        }

        if(host_started)
        {
            USBH_MainDeal();
        }
        BridgeDebug_Tick();
#endif
        MouseBridge_Poll();
    }
}
