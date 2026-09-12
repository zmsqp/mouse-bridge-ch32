#include "usb_bridge_config.h"
#include "mouse_bridge.h"
#include "led_indicator.h"
#include "bridge_debug.h"
#include "bridge_uart_cmd.h"
#include "bridge_usb_cfg.h"
#include "bridge_usb_import.h"
#include "bridge_time.h"
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
    static uint8_t pc_started = 0;
    static uint8_t pc_ready = 0;

    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_1);
    SystemCoreClockUpdate();
    Delay_Init();
    LED_Indicator_Init();
    BridgeDebug_Init();
    BridgeTime_Init();
    BridgeUsbCfg_Init();
    BridgeUsbImport_Init();
    BridgeUart_Init(BRIDGE_DEBUG_BAUD);

    MouseBridge_Init();

    USBFS_RCC_Init();
    Delay_Ms(100);
#if DEF_USBFS_PORT_EN && (USB_PC_PORT == USB_PC_PORT_USBD)
    TIM3_Init(9, SystemCoreClock / 10000 - 1);
    USBFS_Host_Init(ENABLE);
    memset(&RootHubDev.bStatus, 0, sizeof(ROOT_HUB_DEVICE));
    memset(&HostCtl[DEF_USBFS_PORT_INDEX * DEF_ONE_USB_SUP_DEV_TOTAL].InterfaceNum, 0,
           DEF_ONE_USB_SUP_DEV_TOTAL * sizeof(HOST_CTL));
#elif (USB_PC_PORT == USB_PC_PORT_USBFS)
    USBFS_Device_Init(ENABLE);
#endif

    while(1)
    {
        BridgeTime_Poll();
        MouseBridge_Poll();
#if DEF_USBFS_PORT_EN && (USB_PC_PORT == USB_PC_PORT_USBD)
        if(!pc_started)
        {
            static uint32_t host_poll_ms;

            if(BridgeTime_GetMs() != host_poll_ms)
            {
                host_poll_ms = BridgeTime_GetMs();
                USBH_MainDeal();
            }

            if(RootHubDev.bStatus == ROOT_DEV_SUCCESS)
            {
                fSuspendEnabled = FALSE;
                Set_USBConfig();
                USB_Init();
                USB_Interrupts_Config();
                BridgeDebug_LogUsbInit();
                pc_started = 1;
            }
        }
        else if(!pc_ready && bDeviceState == CONFIGURED)
        {
            pc_ready = 1;
            BridgeDebug_LogPcHostReady();
        }
        else if(pc_ready)
        {
            static uint32_t host_poll_ms2;

            if(BridgeTime_GetMs() != host_poll_ms2)
            {
                host_poll_ms2 = BridgeTime_GetMs();
                USBH_MainDeal();
            }
        }
        BridgeDebug_Tick();
#endif
        MouseBridge_Poll();
        BridgeUart_Poll();
        BridgeUsbCfg_Poll();
        BridgeUsbImport_Poll();
        BridgeDebug_Flush();
    }
}
