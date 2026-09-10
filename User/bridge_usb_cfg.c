#include "bridge_usb_cfg.h"
#include "bridge_flash.h"
#include "mouse_bridge.h"
#include "string.h"

static volatile uint8_t g_usb_cfg_pending;
static uint8_t g_usb_rx_buf[BRIDGE_USB_RPT_SIZE];
static uint8_t g_usb_tx_buf[BRIDGE_USB_RPT_SIZE];

static uint8_t BridgeUsbCfg_IsValidCmd(uint8_t cmd)
{
    return (cmd >= BRIDGE_USB_CMD_GET && cmd <= BRIDGE_USB_CMD_MON) ? 1U : 0U;
}

void BridgeUsbCfg_Init(void)
{
    g_usb_cfg_pending = 0;
}

static void BridgeUsbCfg_FillStatus(uint8_t *buf, uint8_t cmd, uint8_t ack)
{
    MouseBridgeConfig *cfg = MouseBridge_GetConfig();
    MouseBridgeLiveState live;

    memset(buf, 0, BRIDGE_USB_RPT_SIZE);
    buf[0] = cmd;
    buf[2] = (uint8_t)((cfg->enabled ? 0x01U : 0U) |
                       (cfg->monitor_stream ? 0x02U : 0U) |
                       (cfg->recoil_springback ? MOUSE_BRIDGE_FLAG_SPRINGBACK : 0U));
    buf[3] = (uint8_t)(cfg->modify_dx & 0xFF);
    buf[4] = (uint8_t)((cfg->modify_dx >> 8) & 0xFF);
    buf[5] = (uint8_t)(cfg->modify_dy & 0xFF);
    buf[6] = (uint8_t)((cfg->modify_dy >> 8) & 0xFF);
    buf[7] = (uint8_t)(cfg->hotkey_hold_ms & 0xFF);
    buf[8] = (uint8_t)((cfg->hotkey_hold_ms >> 8) & 0xFF);

    MouseBridge_GetLiveState(&live);
    buf[9] = live.aim_active;
    buf[10] = live.recoil_active;
    buf[11] = live.buttons;
    buf[12] = ack;
    buf[13] = (uint8_t)(cfg->game_dpi & 0xFF);
    buf[14] = (uint8_t)((cfg->game_dpi >> 8) & 0xFF);
    buf[15] = (uint8_t)(cfg->game_sens_x1000 & 0xFF);
    buf[16] = (uint8_t)((cfg->game_sens_x1000 >> 8) & 0xFF);
    buf[17] = (uint8_t)(cfg->cal_dpi & 0xFF);
    buf[18] = (uint8_t)((cfg->cal_dpi >> 8) & 0xFF);
    buf[19] = (uint8_t)(cfg->cal_sens_x1000 & 0xFF);
    buf[20] = (uint8_t)((cfg->cal_sens_x1000 >> 8) & 0xFF);
    buf[21] = (uint8_t)(cfg->cal_dy_x10 & 0xFF);
    buf[22] = (uint8_t)((cfg->cal_dy_x10 >> 8) & 0xFF);
    buf[23] = 0x53U;
    buf[24] = 0x42U;
}

static void BridgeUsbCfg_HandleReport(const uint8_t *buf)
{
    MouseBridgeConfig *cfg = MouseBridge_GetConfig();

    switch(buf[0])
    {
        case BRIDGE_USB_CMD_SET:
            cfg->modify_dx = (int16_t)(buf[3] | ((int16_t)buf[4] << 8));
            cfg->modify_dy = (int16_t)(buf[5] | ((int16_t)buf[6] << 8));
            cfg->hotkey_hold_ms = (uint16_t)(buf[7] | ((uint16_t)buf[8] << 8));
            if(cfg->hotkey_hold_ms > 3000U)
            {
                cfg->hotkey_hold_ms = 3000U;
            }
            if(cfg->modify_dx > 127)
            {
                cfg->modify_dx = 127;
            }
            if(cfg->modify_dx < -127)
            {
                cfg->modify_dx = -127;
            }
            if(cfg->modify_dy > 127)
            {
                cfg->modify_dy = 127;
            }
            if(cfg->modify_dy < -127)
            {
                cfg->modify_dy = -127;
            }
            cfg->enabled = (buf[2] & 0x01U) ? 1U : 0U;
            cfg->monitor_stream = (buf[2] & 0x02U) ? 1U : 0U;
            cfg->recoil_springback = (buf[2] & MOUSE_BRIDGE_FLAG_SPRINGBACK) ? 1U : 0U;
            if(buf[13] != 0U || buf[14] != 0U)
            {
                cfg->game_dpi = (uint16_t)(buf[13] | ((uint16_t)buf[14] << 8));
            }
            if(buf[15] != 0U || buf[16] != 0U)
            {
                cfg->game_sens_x1000 = (uint16_t)(buf[15] | ((uint16_t)buf[16] << 8));
            }
            if(buf[17] != 0U || buf[18] != 0U)
            {
                cfg->cal_dpi = (uint16_t)(buf[17] | ((uint16_t)buf[18] << 8));
            }
            if(buf[19] != 0U || buf[20] != 0U)
            {
                cfg->cal_sens_x1000 = (uint16_t)(buf[19] | ((uint16_t)buf[20] << 8));
            }
            if(buf[21] != 0U || buf[22] != 0U)
            {
                cfg->cal_dy_x10 = (int16_t)(buf[21] | ((int16_t)buf[22] << 8));
            }
            if(cfg->game_dpi < 100U) { cfg->game_dpi = MOUSE_BRIDGE_DEFAULT_DPI; }
            if(cfg->cal_dpi < 100U) { cfg->cal_dpi = MOUSE_BRIDGE_DEFAULT_DPI; }
            if(cfg->game_sens_x1000 < 10U) { cfg->game_sens_x1000 = MOUSE_BRIDGE_DEFAULT_SENS_X1000; }
            if(cfg->cal_sens_x1000 < 10U) { cfg->cal_sens_x1000 = MOUSE_BRIDGE_DEFAULT_SENS_X1000; }
            cfg->stage_count = 1U;
            cfg->stages[0].duration_ms = 0U;
            cfg->stages[0].dx_x100 = (int16_t)(cfg->modify_dx * 10);
            cfg->stages[0].dy_x100 = (int16_t)(cfg->modify_dy * 10);
            MouseBridge_OnParamsChanged();
            break;

        case BRIDGE_USB_CMD_SAVE:
            BridgeFlash_Save(cfg);
            break;

        case BRIDGE_USB_CMD_MON:
            cfg->monitor_stream = (buf[2] & 0x02U) ? 1U : 0U;
            break;

        default:
            break;
    }
}

void BridgeUsbCfg_OnControlOutDone(void)
{
    extern volatile uint8_t HIDReportOut[];

    if(!BridgeUsbCfg_IsValidCmd(HIDReportOut[0]))
    {
        return;
    }

    memcpy(g_usb_rx_buf, (const void *)HIDReportOut, BRIDGE_USB_RPT_SIZE);
    BridgeUsbCfg_HandleReport(g_usb_rx_buf);
    g_usb_cfg_pending = 0;
}

void BridgeUsbCfg_Poll(void)
{
    if(!g_usb_cfg_pending)
    {
        return;
    }

    g_usb_cfg_pending = 0;
    BridgeUsbCfg_HandleReport(g_usb_rx_buf);
}

void BridgeUsbCfg_FillFeatureReport(uint8_t *buf, uint16_t maxlen)
{
    if(buf == 0 || maxlen < BRIDGE_USB_RPT_SIZE)
    {
        return;
    }

    BridgeUsbCfg_FillStatus(g_usb_tx_buf, BRIDGE_USB_CMD_GET, BRIDGE_USB_ACK_OK);
    memcpy(buf, g_usb_tx_buf, BRIDGE_USB_RPT_SIZE);
}

uint8_t BridgeUsbCfg_ProcessReport(const uint8_t *buf, uint16_t len)
{
    if(buf == 0 || len < BRIDGE_USB_RPT_SIZE || !BridgeUsbCfg_IsValidCmd(buf[0]))
    {
        return 0;
    }

    BridgeUsbCfg_HandleReport(buf);
    return 1;
}
