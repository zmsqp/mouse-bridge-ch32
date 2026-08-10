#include "mouse_bridge.h"
#include "bridge_debug.h"
#include "usb_bridge_config.h"

#if (USB_PC_PORT == USB_PC_PORT_USBFS)
#include "ch32v20x_usbfs_device.h"
#include "usbfs_desc.h"
#else
#include "usb_desc.h"
#include "usb_lib.h"
#include "usb_pwr.h"
#include "usb_prop.h"

extern uint8_t USBD_Endp2_Busy;
extern uint8_t USBD_ENDPx_DataUp(uint8_t endp, uint8_t *pbuf, uint16_t len);
#endif

static MouseBridgeConfig g_cfg;
static uint8_t g_report_pending;
static uint8_t g_pending_len;
static uint8_t g_pending_buf[MOUSE_BRIDGE_REPORT_MAX];

static uint16_t g_ep_max_packet = 4;
static uint8_t g_ep_interval = 1;
static uint8_t g_host_report_id = 0;
static uint8_t g_host_has_report_id = 0;

void MouseBridge_Init(void)
{
    g_cfg.enabled = 1;
    g_cfg.hotkey_active = 0;
    g_cfg.modify_dx = 0;
    g_cfg.modify_dy = 0;
    g_report_pending = 0;

#if (USB_PC_PORT == USB_PC_PORT_USBFS)
    USBFS_LoadDefaultReportDescriptor();
#endif
}

MouseBridgeConfig *MouseBridge_GetConfig(void)
{
    return &g_cfg;
}

void MouseBridge_CloneDeviceDescriptor(const uint8_t *src)
{
    (void)src;
}

void MouseBridge_SetReportDescriptor(const uint8_t *src, uint16_t len)
{
    uint16_t i;

    g_host_report_id = 0;
    g_host_has_report_id = 0;

    if(src == 0 || len == 0)
    {
        return;
    }

    for(i = 0; i + 1 < len; i++)
    {
        if(src[i] == 0x85)
        {
            g_host_report_id = src[i + 1];
            g_host_has_report_id = 1;
            break;
        }
    }
}

void MouseBridge_SetEndpointParams(uint16_t max_packet, uint8_t interval)
{
    if(max_packet == 0)
    {
        max_packet = 4;
    }
    if(max_packet > MOUSE_BRIDGE_REPORT_MAX)
    {
        max_packet = MOUSE_BRIDGE_REPORT_MAX;
    }

    g_ep_max_packet = max_packet;
    g_ep_interval = interval ? interval : 1;
}

void MouseBridge_BuildConfigDescriptor(void)
{
}

void MouseBridge_OnHostReady(void)
{
    BridgeDebug_LogMouseReady(0, g_ep_max_packet, g_host_report_id);
}

static void MouseBridge_ApplyModify(uint8_t *buf, uint16_t len)
{
    if(!g_cfg.hotkey_active || len < 3)
    {
        return;
    }

    if(g_cfg.modify_dx != 0 && len >= 2)
    {
        int16_t dx = (int8_t)buf[1];
        dx += g_cfg.modify_dx;
        if(dx > 127) dx = 127;
        if(dx < -127) dx = -127;
        buf[1] = (uint8_t)dx;
    }

    if(g_cfg.modify_dy != 0 && len >= 3)
    {
        int16_t dy = (int8_t)buf[2];
        dy += g_cfg.modify_dy;
        if(dy > 127) dy = 127;
        if(dy < -127) dy = -127;
        buf[2] = (uint8_t)dy;
    }
}

static void MouseBridge_TrySendPending(void)
{
#if (USB_PC_PORT == USB_PC_PORT_USBFS)
    if(!g_report_pending || !USBFS_DevEnumStatus)
    {
        return;
    }

    if(USBFS_Endp_Busy[DEF_UEP2])
    {
        return;
    }

    if(USBFS_Endp_DataUp(DEF_UEP2, g_pending_buf, g_pending_len, DEF_UEP_CPY_LOAD) == 0)
    {
        g_report_pending = 0;
    }
#else
    uint8_t status;

    if(!g_report_pending || bDeviceState != CONFIGURED)
    {
        return;
    }

    if(USBD_Endp2_Busy)
    {
        return;
    }

    status = USBD_ENDPx_DataUp(ENDP2, g_pending_buf, g_pending_len);
    if(status == USB_SUCCESS)
    {
        g_report_pending = 0;
    }
#endif
}

void MouseBridge_ForwardReport(const uint8_t *data, uint16_t len)
{
    const uint8_t *payload;
    uint16_t copy_len;

    if(!g_cfg.enabled || data == 0 || len == 0)
    {
        return;
    }

    payload = data;
    copy_len = len;

    /* 去掉 Report ID：罗技等接收器常见 1 字节 ID 前缀 */
    if(g_host_has_report_id && copy_len > 0 && data[0] == g_host_report_id)
    {
        payload = data + 1;
        copy_len = len - 1;
    }
    else if(!g_host_has_report_id && copy_len >= 5 &&
            copy_len <= (g_ep_max_packet + 1) && data[0] <= 0x0F)
    {
        payload = data + 1;
        copy_len = len - 1;
    }

    if(copy_len > MOUSE_BRIDGE_REPORT_MAX)
    {
        copy_len = MOUSE_BRIDGE_REPORT_MAX;
    }
    if(copy_len > g_ep_max_packet)
    {
        copy_len = g_ep_max_packet;
    }

    for(uint16_t i = 0; i < copy_len; i++)
    {
        g_pending_buf[i] = payload[i];
    }

    MouseBridge_ApplyModify(g_pending_buf, copy_len);
    g_pending_len = copy_len;
    g_report_pending = 1;
    MouseBridge_TrySendPending();
    BridgeDebug_LogForward(payload, copy_len, g_report_pending ? 0 : 1);
}

void MouseBridge_Poll(void)
{
    MouseBridge_TrySendPending();
}
