#include "mouse_bridge.h"
#include "bridge_debug.h"
#include "usb_bridge_config.h"
#include "bridge_time.h"
#include "bridge_flash.h"
#include "stdio.h"
#include "string.h"

#define MOUSE_BRIDGE_VERBOSE_LAYOUT  0
#define MOUSE_BRIDGE_STATUS_UART     1
#define MOUSE_BRIDGE_RAW_QUEUE_SIZE  64U
#define MOUSE_BRIDGE_LBTN_KEEPALIVE_MS  20U

#if (USB_PC_PORT == USB_PC_PORT_USBFS)
#include "ch32v20x_usbfs_device.h"
#include "usbfs_desc.h"
#else
#include "usb_desc.h"
#include "usb_lib.h"
#include "usb_pwr.h"
#include "usb_prop.h"

extern uint8_t USBD_Endp1_Busy;
extern uint8_t USBD_Endp2_Busy;
extern uint8_t USBD_ENDPx_DataUp(uint8_t endp, uint8_t *pbuf, uint16_t len);
#endif

static MouseBridgeConfig g_cfg;

/* Host 接收器端点包长（可为 64）；PC 侧 USBD 鼠标描述符固定 4 字节 */
static uint16_t g_host_ep_max = 4;
#if (USB_PC_PORT == USB_PC_PORT_USBFS)
static uint16_t g_pc_ep_max = 4;
#else
static uint16_t g_pc_ep_max = DEF_ENDP_SIZE_INJECT;
#endif

/* 从 HID 描述符解析出的鼠标 Input 报告布局（payload，不含 Report ID 前缀字节） */
typedef struct
{
    uint8_t  report_id;
    uint8_t  has_report_id;
    uint16_t btn_bit;
    uint8_t  btn_count;
    uint16_t x_bit;
    uint8_t  x_size;
    uint16_t y_bit;
    uint8_t  y_size;
    uint16_t wheel_bit;
    uint8_t  wheel_size;
    uint8_t  payload_len;  /* 最短 payload 字节数 */
} MouseReportLayout;

static MouseReportLayout g_layout;

/* 位移累加池：USB 忙时只累加计数，发送后再扣减，避免双缓冲覆盖丢手 */
static int16_t g_motion_dx;
static int16_t g_motion_dy;
static uint8_t g_out_buttons;
static int16_t g_out_wheel;
static uint8_t g_last_sent_buttons;
static uint8_t g_motion_dirty;
static uint8_t g_pending_wheel_reports;

#define RECOIL_INJECT_INTERVAL_MS  2U
#define PRIMARY_BUTTON_MASK        0x07U
#define SIDE_BUTTON_MASK           0xF8U
#define AIM_TOGGLE_DEFAULT_MASK    0x10U
#define AIM_TOGGLE_DEBOUNCE_MS     180U

static uint8_t g_aim_btn_was_down;
static uint8_t g_aim_button_mask;
static uint32_t g_last_aim_toggle_ms;
static uint8_t g_current_buttons;
static uint8_t g_forward_buttons;
static uint8_t g_lbtn_was_down;
static uint32_t g_lbtn_down_ms;
static int16_t g_dx_accum_x10;
static int16_t g_dy_accum_x10;
static int16_t g_session_inject_dx;
static int16_t g_session_inject_dy;
static uint32_t g_recoil_start_ms;
static uint8_t g_recoil_stage_index;
static int16_t g_springback_dx;
static int16_t g_springback_dy;
static uint8_t g_springback_active;
static MouseBridgeStage g_zero_stage;
static uint32_t g_stream_last_ms;
static uint32_t g_last_inject_ms;
static uint8_t g_inject_div;
static uint8_t g_raw_debug;
static uint8_t g_raw_last_buttons;
static uint8_t g_raw_drop_seen;
static uint8_t g_raw_last_fail0;
static uint8_t g_raw_queue[MOUSE_BRIDGE_RAW_QUEUE_SIZE][MOUSE_BRIDGE_REPORT_MAX];
static uint16_t g_raw_queue_len[MOUSE_BRIDGE_RAW_QUEUE_SIZE];
static uint8_t g_raw_q_read;
static uint8_t g_raw_q_write;
static uint8_t g_raw_q_count;
static uint8_t g_last_raw_data[MOUSE_BRIDGE_REPORT_MAX];
static uint16_t g_last_raw_len;
static uint32_t g_last_raw_ms;
static uint32_t g_last_lbtn_keepalive_ms;

static void MouseBridge_TryFlush(void);
static void MouseBridge_TryFlushRaw(void);
static void MouseBridge_TryForwardRaw(const uint8_t *data, uint16_t len);
static void MouseBridge_TryLbtnKeepalive(void);
static void MouseBridge_AccumMotion(int16_t dx, int16_t dy, uint8_t buttons, int8_t wheel, uint8_t update_wheel);
static int8_t MouseBridge_ClampAxis(int16_t value);
static void MouseBridge_ApplyDefaultLayout(void);
static void MouseBridge_ParseReportLayout(const uint8_t *desc, uint16_t len);
static uint8_t MouseBridge_ExtractReport(const uint8_t *raw, uint16_t raw_len,
                                           uint8_t *btn, int16_t *dx, int16_t *dy, int8_t *wheel);
static uint8_t MouseBridge_ExtractButtons(const uint8_t *raw, uint16_t raw_len, uint8_t *btn);
static uint8_t MouseBridge_ExtractBootPayload(const uint8_t *payload, uint16_t plen,
                                              uint8_t *btn, int16_t *dx, int16_t *dy, int8_t *wheel);
static uint32_t MouseBridge_ReadBits(const uint8_t *p, uint16_t plen, uint16_t bit, uint8_t size);
static int16_t MouseBridge_ReadSignedBits(const uint8_t *p, uint16_t plen, uint16_t bit, uint8_t size);
static void MouseBridge_ResetRecoilAccum(void);
static void MouseBridge_CancelSpringback(void);
static void MouseBridge_ResetRecoilSession(void);
static void MouseBridge_LogRawReport(const uint8_t *data, uint16_t len,
                                     uint8_t parsed, uint8_t buttons,
                                     int16_t dx, int16_t dy, int8_t wheel);
static uint8_t MouseBridge_SelectSideButton(uint8_t pressed);

static void MouseBridge_NotifyAimChange(void)
{
#if MOUSE_BRIDGE_STATUS_UART
    printf("@A,%u\r\n", (unsigned)g_cfg.aim_active);
#else
    if(!g_cfg.monitor_stream)
    {
        return;
    }

    printf("@A,%u\r\n", (unsigned)g_cfg.aim_active);
#endif
}

static void MouseBridge_NotifyHotkeyChange(void)
{
#if MOUSE_BRIDGE_STATUS_UART
    printf("@H,%u\r\n", (unsigned)g_cfg.hotkey_active);
#else
    if(!g_cfg.monitor_stream)
    {
        return;
    }

    printf("@H,%u\r\n", (unsigned)g_cfg.hotkey_active);
#endif
    g_stream_last_ms = 0;
}

static void MouseBridge_LogRawReport(const uint8_t *data, uint16_t len,
                                     uint8_t parsed, uint8_t buttons,
                                     int16_t dx, int16_t dy, int8_t wheel)
{
    uint16_t i;
    uint16_t show;

    if(!g_raw_debug)
    {
        return;
    }
    if(parsed)
    {
        if(buttons == g_raw_last_buttons)
        {
            return;
        }
        g_raw_last_buttons = buttons;
    }
    else
    {
        uint8_t fail0 = (data != 0 && len > 0U) ? data[0] : 0U;
        if(g_raw_drop_seen && fail0 == g_raw_last_fail0)
        {
            return;
        }
        g_raw_drop_seen = 1U;
        g_raw_last_fail0 = fail0;
    }

    show = (len > 12U) ? 12U : len;
    printf("@R,%u,%u,%u,%d,%d,%d,%u,",
           (unsigned)parsed,
           (unsigned)buttons,
           (unsigned)g_aim_button_mask,
           (int)dx,
           (int)dy,
           (int)wheel,
           (unsigned)len);
    for(i = 0; i < show; i++)
    {
        printf("%02X", data[i]);
        if(i + 1U < show)
        {
            printf(" ");
        }
    }
    printf("\r\n");
}

static uint8_t MouseBridge_SelectSideButton(uint8_t pressed)
{
    uint8_t side = (uint8_t)(pressed & SIDE_BUTTON_MASK);

    if(side == 0U)
    {
        return 0U;
    }
    if(side & AIM_TOGGLE_DEFAULT_MASK)
    {
        return AIM_TOGGLE_DEFAULT_MASK;
    }
    if(side & 0x08U)
    {
        return 0x08U;
    }
    if(side & 0x20U)
    {
        return 0x20U;
    }
    if(side & 0x40U)
    {
        return 0x40U;
    }
    if(side & 0x80U)
    {
        return 0x80U;
    }
    return side;
}

void MouseBridge_Init(void)
{
    g_aim_btn_was_down = 0;
    g_aim_button_mask = AIM_TOGGLE_DEFAULT_MASK;
    g_last_aim_toggle_ms = 0;
    g_motion_dx = 0;
    g_motion_dy = 0;
    g_out_buttons = 0;
    g_out_wheel = 0;
    g_last_sent_buttons = 0xFFU;
    g_motion_dirty = 0;
    g_pending_wheel_reports = 0;
    g_current_buttons = 0;
    g_forward_buttons = 0;
    g_lbtn_was_down = 0;
    g_lbtn_down_ms = 0;
    g_dx_accum_x10 = 0;
    g_dy_accum_x10 = 0;
    g_session_inject_dx = 0;
    g_session_inject_dy = 0;
    g_recoil_start_ms = 0;
    g_recoil_stage_index = 0;
    g_springback_dx = 0;
    g_springback_dy = 0;
    g_springback_active = 0;
    g_zero_stage.duration_ms = 0U;
    g_zero_stage.dx_x10 = 0;
    g_zero_stage.dy_x10 = 0;
    g_last_inject_ms = 0;
    g_inject_div = 0;
    g_raw_debug = 0;
    g_raw_last_buttons = 0xFFU;
    g_raw_drop_seen = 0;
    g_raw_last_fail0 = 0xFFU;
    g_raw_q_read = 0;
    g_raw_q_write = 0;
    g_raw_q_count = 0;
    g_last_raw_len = 0;
    g_last_raw_ms = 0;
    g_last_lbtn_keepalive_ms = 0;

    BridgeFlash_Load(&g_cfg);
    MouseBridge_ApplyDefaultLayout();
#if (USB_PC_PORT == USB_PC_PORT_USBD)
    USBD_LoadDefaultReportDescriptor();
#endif
    g_cfg.hotkey_active = 0;
    g_cfg.aim_active = 0;
    g_cfg.monitor_stream = 0;

#if (USB_PC_PORT == USB_PC_PORT_USBFS)
    USBFS_LoadDefaultReportDescriptor();
#endif
}

void MouseBridge_TickMs(void)
{
}

void MouseBridge_GetLiveState(MouseBridgeLiveState *state)
{
    if(state == 0)
    {
        return;
    }

    state->aim_active = g_cfg.aim_active;
    state->recoil_active = g_cfg.hotkey_active;
    state->buttons = g_current_buttons;
}

MouseBridgeConfig *MouseBridge_GetConfig(void)
{
    return &g_cfg;
}

void MouseBridge_SetRawDebug(uint8_t enabled)
{
    g_raw_debug = enabled ? 1U : 0U;
}

void MouseBridge_CloneDeviceDescriptor(const uint8_t *src)
{
    (void)src;
}

void MouseBridge_OnParamsChanged(void)
{
    MouseBridge_ResetRecoilAccum();
    MouseBridge_CancelSpringback();
    MouseBridge_ResetRecoilSession();
    g_cfg.hotkey_active = 0;
    if(g_cfg.stage_count == 0U || g_cfg.stage_count > MOUSE_BRIDGE_PROFILE_STAGES)
    {
        g_cfg.stage_count = 1U;
    }
    g_cfg.modify_dx = g_cfg.stages[0].dx_x10;
    g_cfg.modify_dy = g_cfg.stages[0].dy_x10;

    if(!g_cfg.enabled)
    {
        g_cfg.aim_active = 0;
    }
}

void MouseBridge_SetReportDescriptor(const uint8_t *src, uint16_t len)
{
    MouseBridge_ApplyDefaultLayout();
    if(src != 0 && len > 0)
    {
        MouseBridge_ParseReportLayout(src, len);
#if (USB_PC_PORT == USB_PC_PORT_USBD)
        USBD_LoadPassthroughReportDescriptor(src, len);
#endif
    }
}

void MouseBridge_UseBootMouseLayout(void)
{
    MouseBridge_ApplyDefaultLayout();
}

static void MouseBridge_ApplyDefaultLayout(void)
{
    g_layout.report_id = 0;
    g_layout.has_report_id = 0;
    g_layout.btn_bit = 0;
    g_layout.btn_count = 5U;
    g_layout.x_bit = 8;
    g_layout.x_size = 8;
    g_layout.y_bit = 16;
    g_layout.y_size = 8;
    g_layout.wheel_bit = 24;
    g_layout.wheel_size = 8;
    g_layout.payload_len = 4;
}

static uint16_t MouseBridge_HidItemDataSize(uint8_t hdr)
{
    uint8_t sz = hdr & 0x03U;

    if(sz == 3U)
    {
        return 4U;
    }
    return sz;
}

static uint32_t MouseBridge_ReadBits(const uint8_t *p, uint16_t plen, uint16_t bit, uint8_t size)
{
    uint32_t value = 0U;
    uint8_t i;

    if(p == 0 || size == 0U || size > 32U)
    {
        return 0U;
    }

    for(i = 0; i < size; i++)
    {
        uint16_t src_bit = (uint16_t)(bit + i);
        uint16_t byte_pos = (uint16_t)(src_bit >> 3);

        if(byte_pos >= plen)
        {
            break;
        }
        if((p[byte_pos] & (uint8_t)(1U << (src_bit & 0x07U))) != 0U)
        {
            value |= (uint32_t)1U << i;
        }
    }

    return value;
}

static int16_t MouseBridge_ReadSignedBits(const uint8_t *p, uint16_t plen, uint16_t bit, uint8_t size)
{
    uint32_t value;
    uint32_t sign;

    if(size == 0U)
    {
        return 0;
    }
    if(size > 16U)
    {
        size = 16U;
    }

    value = MouseBridge_ReadBits(p, plen, bit, size);
    sign = (uint32_t)1U << (size - 1U);
    if((value & sign) != 0U)
    {
        value |= (~0UL << size);
    }

    return (int16_t)value;
}

static uint8_t MouseBridge_ExtractBootPayload(const uint8_t *payload, uint16_t plen,
                                              uint8_t *btn, int16_t *dx, int16_t *dy, int8_t *wheel)
{
    if(payload == 0 || plen < 3U || btn == 0 || dx == 0 || dy == 0 || wheel == 0)
    {
        return 0U;
    }

    *btn = payload[0];
    *dx = (int8_t)payload[1];
    *dy = (int8_t)payload[2];
    *wheel = (plen >= 4U) ? (int8_t)payload[3] : 0;
    return 1U;
}

static void MouseBridge_ParseReportLayout(const uint8_t *desc, uint16_t len)
{
    uint16_t i = 0;
    uint16_t bit_pos = 0;
    uint8_t report_size = 0;
    uint8_t report_count = 0;
    uint8_t in_mouse_app = 0;
    uint8_t in_ptr_col = 0;
    uint8_t usage_page = 0;
    uint8_t mouse_usage_seen = 0;
    uint8_t usage_min = 0;
    uint8_t usage_max = 0;
    uint8_t has_usage_min = 0;
    uint8_t has_usage_max = 0;
    uint8_t pending_usages[8];
    uint8_t pending_count = 0;

    MouseBridge_ApplyDefaultLayout();

    while(i < len)
    {
        uint8_t hdr = desc[i];
        uint16_t dsz = MouseBridge_HidItemDataSize(hdr);
        uint8_t typ = (hdr >> 2) & 0x03U;
        uint8_t tag = (hdr >> 4) & 0x0FU;
        const uint8_t *dat = &desc[i + 1U];

        if(i + 1U + dsz > len)
        {
            break;
        }

        if(typ == 0x01U)
        {
            /* Global */
            if(tag == 0x0U && dsz >= 1U)
            {
                usage_page = dat[0];
            }
            else if(tag == 0x8U && dsz >= 1U && in_mouse_app)
            {
                g_layout.report_id = dat[0];
                g_layout.has_report_id = 1U;
                bit_pos = 0;
                pending_count = 0;
            }
            else if(tag == 0x7U && dsz >= 1U)
            {
                report_size = dat[0];
            }
            else if(tag == 0x9U && dsz >= 1U)
            {
                report_count = dat[0];
            }
        }
        else if(typ == 0x2U)
        {
            /* Local Usage */
            if(tag == 0x0U && dsz >= 1U)
            {
                if(usage_page == 0x01U && dat[0] == 0x02U)
                {
                    mouse_usage_seen = 1U;
                }
                if(in_mouse_app && pending_count < 8U)
                {
                    pending_usages[pending_count++] = dat[0];
                }
            }
            else if(tag == 0x1U && dsz >= 1U)
            {
                usage_min = dat[0];
                has_usage_min = 1U;
            }
            else if(tag == 0x2U && dsz >= 1U)
            {
                usage_max = dat[0];
                has_usage_max = 1U;
            }
        }
        else if(typ == 0x0U)
        {
            if(tag == 0xAU)
            {
                /* Collection */
                if(dsz >= 1U && dat[0] == 0x01U && mouse_usage_seen)
                {
                    in_mouse_app = 1U;
                    in_ptr_col = 0U;
                    mouse_usage_seen = 0U;
                    bit_pos = 0;
                    pending_count = 0;
                    has_usage_min = 0U;
                    has_usage_max = 0U;
                }
                else if(dsz >= 1U && in_mouse_app && dat[0] == 0x00U)
                {
                    in_ptr_col = 1U;
                    pending_count = 0;
                    has_usage_min = 0U;
                    has_usage_max = 0U;
                }
            }
            else if(tag == 0xC0U)
            {
                if(in_ptr_col)
                {
                    in_ptr_col = 0U;
                }
                else if(in_mouse_app)
                {
                    in_mouse_app = 0U;
                }
                pending_count = 0;
                has_usage_min = 0U;
                has_usage_max = 0U;
            }
            else if(tag == 0x8U && in_mouse_app)
            {
                uint16_t bits = (uint16_t)report_size * (uint16_t)report_count;
                uint8_t u;
                uint8_t is_data = (dsz >= 1U) && ((dat[0] & 0x01U) == 0U);

                if(is_data && bits > 0U)
                {
                    if(usage_page == 0x09U)
                    {
                        uint8_t count = report_count;

                        if(has_usage_min && has_usage_max && usage_min >= 1U && usage_max >= usage_min)
                        {
                            count = (uint8_t)(usage_max - usage_min + 1U);
                            if(count > report_count)
                            {
                                count = report_count;
                            }
                        }
                        else if(pending_count > 0U)
                        {
                            count = pending_count;
                        }

                        if(count > 8U)
                        {
                            count = 8U;
                        }

                        if(count > 0U)
                        {
                            g_layout.btn_bit = bit_pos;
                            g_layout.btn_count = count;
                        }
                    }
                    else if(usage_page == 0x01U)
                    {
                        for(u = 0; u < pending_count && u < report_count; u++)
                        {
                            uint16_t field_bit = (uint16_t)(bit_pos + ((uint16_t)u * report_size));

                            if(pending_usages[u] == 0x30U)
                            {
                                g_layout.x_bit = field_bit;
                                g_layout.x_size = report_size;
                            }
                            else if(pending_usages[u] == 0x31U)
                            {
                                g_layout.y_bit = field_bit;
                                g_layout.y_size = report_size;
                            }
                            else if(pending_usages[u] == 0x38U)
                            {
                                g_layout.wheel_bit = field_bit;
                                g_layout.wheel_size = report_size;
                            }
                        }
                    }
                }

                bit_pos = (uint16_t)(bit_pos + bits);
                pending_count = 0;
                has_usage_min = 0U;
                has_usage_max = 0U;
            }
        }

        i = (uint16_t)(i + 1U + dsz);
    }

    g_layout.payload_len = (uint8_t)((bit_pos + 7U) / 8U);
    if(g_layout.payload_len < 3U)
    {
        g_layout.payload_len = 4U;
    }
}

static uint8_t MouseBridge_ExtractReport(const uint8_t *raw, uint16_t raw_len,
                                         uint8_t *btn, int16_t *dx, int16_t *dy, int8_t *wheel)
{
    const uint8_t *payload;
    uint16_t plen;

    if(raw == 0 || raw_len == 0U || btn == 0 || dx == 0 || dy == 0 || wheel == 0)
    {
        return 0U;
    }

    if(g_layout.has_report_id)
    {
        if(raw[0] != g_layout.report_id)
        {
            if(raw_len >= 5U &&
               MouseBridge_ExtractBootPayload(raw + 1U, (uint16_t)(raw_len - 1U), btn, dx, dy, wheel))
            {
                g_layout.report_id = raw[0];
                g_layout.btn_bit = 0U;
                g_layout.btn_count = 8U;
                g_layout.x_bit = 8U;
                g_layout.x_size = 8U;
                g_layout.y_bit = 16U;
                g_layout.y_size = 8U;
                g_layout.wheel_bit = 24U;
                g_layout.wheel_size = 8U;
                g_layout.payload_len = 4U;
                return 1U;
            }
            return 0U;
        }
        payload = raw + 1U;
        plen = (uint16_t)(raw_len - 1U);
    }
    else
    {
        payload = raw;
        plen = raw_len;
    }

    if(plen < g_layout.payload_len)
    {
        return MouseBridge_ExtractBootPayload(payload, plen, btn, dx, dy, wheel);
    }

    *btn = (uint8_t)(MouseBridge_ReadBits(payload, plen, g_layout.btn_bit, g_layout.btn_count) & 0xFFU);
    *dx = MouseBridge_ReadSignedBits(payload, plen, g_layout.x_bit, g_layout.x_size);
    *dy = MouseBridge_ReadSignedBits(payload, plen, g_layout.y_bit, g_layout.y_size);
    if(g_layout.wheel_size > 0U)
    {
        *wheel = (int8_t)MouseBridge_ReadSignedBits(payload, plen, g_layout.wheel_bit, g_layout.wheel_size);
    }
    else
    {
        *wheel = 0;
    }

    return 1U;
}

static uint8_t MouseBridge_ExtractButtons(const uint8_t *raw, uint16_t raw_len, uint8_t *btn)
{
    const uint8_t *payload;
    uint16_t plen;

    if(raw == 0 || raw_len == 0U || btn == 0)
    {
        return 0U;
    }

    if(g_layout.has_report_id)
    {
        if(raw[0] != g_layout.report_id)
        {
            return 0U;
        }
        payload = raw + 1U;
        plen = (uint16_t)(raw_len - 1U);
    }
    else
    {
        payload = raw;
        plen = raw_len;
    }

    if(((uint16_t)g_layout.btn_bit + g_layout.btn_count + 7U) / 8U > plen)
    {
        return 0U;
    }

    *btn = (uint8_t)(MouseBridge_ReadBits(payload, plen, g_layout.btn_bit, g_layout.btn_count) & 0xFFU);
    return 1U;
}

static uint8_t MouseBridge_ModifyActive(void)
{
    uint8_t i;

    for(i = 0; i < g_cfg.stage_count && i < MOUSE_BRIDGE_PROFILE_STAGES; i++)
    {
        if(g_cfg.stages[i].dx_x10 != 0 || g_cfg.stages[i].dy_x10 != 0)
        {
            return 1U;
        }
    }
    return 0U;
}

void MouseBridge_OnHostReady(void)
{
    BridgeDebug_LogMouseReady(0, g_pc_ep_max, g_layout.report_id);
#if BRIDGE_DEBUG_UART && MOUSE_BRIDGE_VERBOSE_LAYOUT
    printf("[Host] layout rid=%u btnbit=%u/%u xbit=%u/%u ybit=%u/%u wbit=%u/%u pay=%u\r\n",
           g_layout.has_report_id ? (unsigned)g_layout.report_id : 0U,
           (unsigned)g_layout.btn_bit, (unsigned)g_layout.btn_count,
           (unsigned)g_layout.x_bit, (unsigned)g_layout.x_size,
           (unsigned)g_layout.y_bit, (unsigned)g_layout.y_size,
           (unsigned)g_layout.wheel_bit, (unsigned)g_layout.wheel_size,
           (unsigned)g_layout.payload_len);
#endif
}

void MouseBridge_SetEndpointParams(uint16_t max_packet, uint8_t interval)
{
    (void)interval;

    if(max_packet == 0)
    {
        max_packet = 4;
    }
    if(max_packet > MOUSE_BRIDGE_REPORT_MAX)
    {
        max_packet = MOUSE_BRIDGE_REPORT_MAX;
    }

    g_host_ep_max = max_packet;
}

void MouseBridge_BuildConfigDescriptor(void)
{
}

static void MouseBridge_ResetRecoilAccum(void)
{
    g_dx_accum_x10 = 0;
    g_dy_accum_x10 = 0;
}

static void MouseBridge_CancelSpringback(void)
{
    g_springback_active = 0U;
    g_springback_dx = 0;
    g_springback_dy = 0;
}

static void MouseBridge_ResetRecoilSession(void)
{
    g_session_inject_dx = 0;
    g_session_inject_dy = 0;
    g_recoil_start_ms = 0;
    g_recoil_stage_index = 0;
    MouseBridge_ResetRecoilAccum();
}

static void MouseBridge_TrackSessionInject(int8_t dx, int8_t dy)
{
    g_session_inject_dx = (int16_t)(g_session_inject_dx + dx);
    g_session_inject_dy = (int16_t)(g_session_inject_dy + dy);
}

static void MouseBridge_StartSpringback(void)
{
    if(!g_cfg.recoil_springback)
    {
        MouseBridge_ResetRecoilSession();
        return;
    }

    if(g_session_inject_dx == 0 && g_session_inject_dy == 0)
    {
        return;
    }

    g_springback_dx = (int16_t)(-g_session_inject_dx);
    g_springback_dy = (int16_t)(-g_session_inject_dy);
    g_session_inject_dx = 0;
    g_session_inject_dy = 0;
    g_springback_active = 1U;
    MouseBridge_ResetRecoilAccum();
}

static void MouseBridge_TickSpringback(void)
{
    int8_t dx;
    int8_t dy;

    if(!g_springback_active)
    {
        return;
    }

    dx = MouseBridge_ClampAxis(g_springback_dx);
    dy = MouseBridge_ClampAxis(g_springback_dy);

    if(dx == 0 && dy == 0)
    {
        if(g_springback_dx > 0)
        {
            dx = 1;
        }
        else if(g_springback_dx < 0)
        {
            dx = -1;
        }
        if(g_springback_dy > 0)
        {
            dy = 1;
        }
        else if(g_springback_dy < 0)
        {
            dy = -1;
        }
    }

    if(dx == 0 && dy == 0)
    {
        g_springback_active = 0U;
        return;
    }

    MouseBridge_AccumMotion(dx, dy, 0U, 0, 0U);
    g_springback_dx = (int16_t)(g_springback_dx - dx);
    g_springback_dy = (int16_t)(g_springback_dy - dy);

    if(g_springback_dx == 0 && g_springback_dy == 0)
    {
        g_springback_active = 0U;
    }
}

static void MouseBridge_HandleButtons(uint8_t buttons)
{
    uint8_t lbtn = (uint8_t)(buttons & 0x01U);
    uint8_t pressed = (uint8_t)(buttons & (uint8_t)~g_current_buttons);
    uint8_t side_pressed = MouseBridge_SelectSideButton(pressed);
    uint8_t aim_btn;
    uint32_t now = BridgeTime_GetMs();
    uint8_t aim_debounce_ok = (g_last_aim_toggle_ms == 0U ||
                               (now - g_last_aim_toggle_ms) >= AIM_TOGGLE_DEBOUNCE_MS);

    if(side_pressed != 0U && aim_debounce_ok)
    {
        g_aim_button_mask = side_pressed;
        g_cfg.aim_active = g_cfg.aim_active ? 0U : 1U;
        g_last_aim_toggle_ms = now;
        MouseBridge_NotifyAimChange();
    }
    else
    {
        aim_btn = (uint8_t)((buttons & g_aim_button_mask) ? 1U : 0U);
        if(aim_btn && !g_aim_btn_was_down && aim_debounce_ok)
        {
            g_cfg.aim_active = g_cfg.aim_active ? 0U : 1U;
            g_last_aim_toggle_ms = now;
            MouseBridge_NotifyAimChange();
        }
    }

    aim_btn = (uint8_t)((buttons & g_aim_button_mask) ? 1U : 0U);
    g_aim_btn_was_down = aim_btn;

    if(lbtn && !g_lbtn_was_down)
    {
        g_lbtn_down_ms = now;
    }

    g_lbtn_was_down = lbtn;
    g_current_buttons = buttons;
}

static void MouseBridge_TickRecoilLogic(void)
{
    uint8_t lbtn = (uint8_t)(g_current_buttons & 0x01U);
    uint8_t prev_active = g_cfg.hotkey_active;
    uint32_t now = BridgeTime_GetMs();

    if(!g_cfg.aim_active)
    {
        g_cfg.hotkey_active = 0;
        MouseBridge_CancelSpringback();
        MouseBridge_ResetRecoilSession();
    }
    else if(lbtn)
    {
        uint32_t held = now - g_lbtn_down_ms;

        if(held >= g_cfg.hotkey_hold_ms)
        {
            g_cfg.hotkey_active = MouseBridge_ModifyActive() ? 1U : 0U;
        }
        else
        {
            g_cfg.hotkey_active = 0U;
        }

        if(g_cfg.hotkey_active && !prev_active)
        {
            MouseBridge_CancelSpringback();
            MouseBridge_ResetRecoilSession();
        }
    }
    else
    {
        if(prev_active)
        {
            MouseBridge_StartSpringback();
        }
        g_cfg.hotkey_active = 0;
    }

    if(g_cfg.hotkey_active != prev_active)
    {
        MouseBridge_NotifyHotkeyChange();
    }
}

static int8_t MouseBridge_ClampAxis(int16_t value)
{
    if(value > 127)
    {
        return 127;
    }
    if(value < -127)
    {
        return -127;
    }
    return (int8_t)value;
}

static int8_t MouseBridge_ConsumeModifyStep(int16_t modify_x10, int16_t *accum)
{
    int8_t step = 0;

    if(modify_x10 == 0)
    {
        return 0;
    }

    *accum += modify_x10;
    while(*accum >= 10)
    {
        step++;
        *accum = (int16_t)(*accum - 10);
    }
    while(*accum <= -10)
    {
        step--;
        *accum = (int16_t)(*accum + 10);
    }

    return step;
}

static const MouseBridgeStage *MouseBridge_CurrentStage(void)
{
    uint32_t elapsed;
    uint32_t sum = 0U;
    uint8_t i;

    if(g_cfg.stage_count == 0U || g_cfg.stage_count > MOUSE_BRIDGE_PROFILE_STAGES)
    {
        g_cfg.stage_count = 1U;
    }

    if(g_recoil_start_ms == 0U)
    {
        g_recoil_start_ms = BridgeTime_GetMs();
        g_recoil_stage_index = 0U;
    }

    elapsed = BridgeTime_GetMs() - g_recoil_start_ms;
    for(i = 0; i < g_cfg.stage_count; i++)
    {
        uint16_t dur = g_cfg.stages[i].duration_ms;

        if(dur == 0U)
        {
            g_recoil_stage_index = i;
            if(g_cfg.stage_count == 1U)
            {
                return &g_cfg.stages[i];
            }
            return &g_zero_stage;
        }
        sum += dur;
        if(elapsed < sum)
        {
            g_recoil_stage_index = i;
            return &g_cfg.stages[i];
        }
    }

    g_recoil_stage_index = (uint8_t)(g_cfg.stage_count - 1U);
    return &g_zero_stage;
}

static void MouseBridge_StreamReport(uint8_t buttons, int16_t dx, int16_t dy, int8_t wheel)
{
    uint32_t now;

    if(!g_cfg.monitor_stream)
    {
        return;
    }

    now = BridgeTime_GetMs();
    if((now - g_stream_last_ms) < 50U)
    {
        return;
    }
    g_stream_last_ms = now;

    printf("@M,%u,%d,%d,%d,%u,%u\r\n",
           (unsigned)buttons,
           (int)MouseBridge_ClampAxis(dx),
           (int)MouseBridge_ClampAxis(dy),
           (int)wheel,
           (unsigned)g_cfg.hotkey_active,
           (unsigned)g_cfg.aim_active);
}

static void MouseBridge_AccumMotion(int16_t dx, int16_t dy, uint8_t buttons, int8_t wheel, uint8_t update_wheel)
{
    g_motion_dx += dx;
    g_motion_dy += dy;
    g_out_buttons = buttons;
    if(update_wheel && wheel != 0)
    {
        g_out_wheel = (int16_t)(g_out_wheel + wheel);
        if(g_pending_wheel_reports < 255U)
        {
            g_pending_wheel_reports++;
        }
    }
    g_motion_dirty = 1U;
    MouseBridge_TryFlush();
}

static uint8_t MouseBridge_UsbReady(void)
{
#if (USB_PC_PORT == USB_PC_PORT_USBFS)
    return USBFS_DevEnumStatus ? 1U : 0U;
#else
    return (bDeviceState == CONFIGURED) ? 1U : 0U;
#endif
}

static uint8_t MouseBridge_EndpBusy(void)
{
#if (USB_PC_PORT == USB_PC_PORT_USBFS)
    return USBFS_Endp_Busy[DEF_UEP2] ? 1U : 0U;
#else
    return USBD_Endp2_Busy ? 1U : 0U;
#endif
}

static uint8_t MouseBridge_SendPacket(const uint8_t *buf, uint16_t len)
{
#if (USB_PC_PORT == USB_PC_PORT_USBFS)
    if(USBFS_Endp_DataUp(DEF_UEP2, (uint8_t *)buf, len, DEF_UEP_CPY_LOAD) == 0)
    {
        return 1U;
    }
    return 0U;
#else
    return (USBD_ENDPx_DataUp(ENDP2, (uint8_t *)buf, len) == USB_SUCCESS) ? 1U : 0U;
#endif
}

static uint8_t MouseBridge_RawEndpBusy(void)
{
#if (USB_PC_PORT == USB_PC_PORT_USBFS)
    return USBFS_Endp_Busy[DEF_UEP2] ? 1U : 0U;
#else
    return USBD_Endp1_Busy ? 1U : 0U;
#endif
}

static uint8_t MouseBridge_SendRawPacket(const uint8_t *buf, uint16_t len)
{
#if (USB_PC_PORT == USB_PC_PORT_USBFS)
    if(USBFS_Endp_DataUp(DEF_UEP2, (uint8_t *)buf, len, DEF_UEP_CPY_LOAD) == 0)
    {
        return 1U;
    }
    return 0U;
#else
    return (USBD_ENDPx_DataUp(ENDP1, (uint8_t *)buf, len) == USB_SUCCESS) ? 1U : 0U;
#endif
}

static void MouseBridge_TryFlushRaw(void)
{
    if(!MouseBridge_UsbReady() || MouseBridge_RawEndpBusy())
    {
        return;
    }

    while(g_raw_q_count > 0U && !MouseBridge_RawEndpBusy())
    {
        if(!MouseBridge_SendRawPacket(g_raw_queue[g_raw_q_read], g_raw_queue_len[g_raw_q_read]))
        {
            break;
        }

        g_raw_q_read = (uint8_t)((g_raw_q_read + 1U) % MOUSE_BRIDGE_RAW_QUEUE_SIZE);
        g_raw_q_count--;
    }
}

static void MouseBridge_TryForwardRaw(const uint8_t *data, uint16_t len)
{
    if(data == 0 || len == 0U)
    {
        return;
    }

    if(len > MOUSE_BRIDGE_REPORT_MAX)
    {
        len = MOUSE_BRIDGE_REPORT_MAX;
    }

    memcpy(g_last_raw_data, data, len);
    g_last_raw_len = len;
    g_last_raw_ms = BridgeTime_GetMs();

    MouseBridge_TryFlushRaw();
    if(g_raw_q_count == 0U && MouseBridge_UsbReady() && !MouseBridge_RawEndpBusy())
    {
        if(MouseBridge_SendRawPacket(data, len))
        {
            return;
        }
    }

    if(g_raw_q_count >= MOUSE_BRIDGE_RAW_QUEUE_SIZE)
    {
        g_raw_q_read = (uint8_t)((g_raw_q_read + 1U) % MOUSE_BRIDGE_RAW_QUEUE_SIZE);
        g_raw_q_count--;
    }

    memcpy(g_raw_queue[g_raw_q_write], data, len);
    g_raw_queue_len[g_raw_q_write] = len;
    g_raw_q_write = (uint8_t)((g_raw_q_write + 1U) % MOUSE_BRIDGE_RAW_QUEUE_SIZE);
    g_raw_q_count++;
}

static void MouseBridge_TryLbtnKeepalive(void)
{
    uint32_t now;

#if MOUSE_BRIDGE_LBTN_KEEPALIVE_MS == 0U
    return;
#endif
    if((g_current_buttons & 0x01U) == 0U || g_last_raw_len == 0U)
    {
        return;
    }
    if(g_raw_q_count > 0U || MouseBridge_RawEndpBusy())
    {
        return;
    }

    now = BridgeTime_GetMs();
    if((now - g_last_raw_ms) < MOUSE_BRIDGE_LBTN_KEEPALIVE_MS ||
       (now - g_last_lbtn_keepalive_ms) < MOUSE_BRIDGE_LBTN_KEEPALIVE_MS)
    {
        return;
    }

    g_last_lbtn_keepalive_ms = now;
    MouseBridge_TryForwardRaw(g_last_raw_data, g_last_raw_len);
}

static void MouseBridge_TryFlush(void)
{
    uint8_t pkt[MOUSE_BRIDGE_REPORT_MAX];
    uint16_t len;
    int8_t sdx;
    int8_t sdy;
    int8_t swheel;

    if(!g_motion_dirty || !MouseBridge_UsbReady())
    {
        return;
    }

    len = g_pc_ep_max;
    if(len < 3U)
    {
        return;
    }

    while(g_motion_dirty && !MouseBridge_EndpBusy())
    {
        sdx = MouseBridge_ClampAxis(g_motion_dx);
        sdy = MouseBridge_ClampAxis(g_motion_dy);
        swheel = (len >= 4U) ? MouseBridge_ClampAxis(g_out_wheel) : 0;

        if(sdx == 0 && sdy == 0 &&
           g_out_buttons == g_last_sent_buttons &&
           swheel == 0)
        {
            g_motion_dirty = 0U;
            break;
        }

        pkt[0] = (uint8_t)sdx;
        pkt[1] = (uint8_t)sdy;
        pkt[2] = (uint8_t)swheel;
        if(len >= 4U)
        {
            pkt[3] = 0U;
        }

        if(!MouseBridge_SendPacket(pkt, len))
        {
            BridgeDebug_LogForward(pkt, len, 0U);
            break;
        }

        BridgeDebug_LogForward(pkt, len, 1U);

        g_motion_dx = (int16_t)(g_motion_dx - sdx);
        g_motion_dy = (int16_t)(g_motion_dy - sdy);
        g_last_sent_buttons = g_out_buttons;
        if(len >= 4U)
        {
            g_out_wheel = (int16_t)(g_out_wheel - swheel);
            if(swheel != 0 && g_pending_wheel_reports > 0U)
            {
                g_pending_wheel_reports--;
            }
        }

        MouseBridge_StreamReport(g_out_buttons, sdx, sdy, swheel);

        if(g_motion_dx == 0 && g_motion_dy == 0 &&
           g_out_buttons == g_last_sent_buttons &&
           (len < 4U || g_out_wheel == 0))
        {
            g_motion_dirty = 0U;
        }
    }
}

static void MouseBridge_DispatchReport(uint8_t buttons, int16_t dx, int16_t dy, int8_t wheel, uint8_t from_host)
{
    uint8_t out_buttons = buttons;

    if(from_host)
    {
        if(g_cfg.enabled)
        {
            MouseBridge_HandleButtons(buttons);
            out_buttons = (uint8_t)(buttons & (uint8_t)~g_aim_button_mask);
        }
        else
        {
            g_current_buttons = buttons;
        }
        g_forward_buttons = out_buttons;
    }

    MouseBridge_AccumMotion(dx, dy, out_buttons, wheel, 1U);

    if(from_host && g_cfg.monitor_stream)
    {
        MouseBridge_StreamReport(buttons, dx, dy, wheel);
    }
}

static void MouseBridge_InjectRecoil(void)
{
    uint32_t now;
    int8_t dx;
    int8_t dy;
    const MouseBridgeStage *stage;

    if(g_springback_active)
    {
        return;
    }

    if(!g_cfg.enabled || !g_cfg.hotkey_active || !MouseBridge_ModifyActive())
    {
        return;
    }

    now = BridgeTime_GetMs();
    if((now - g_last_inject_ms) < RECOIL_INJECT_INTERVAL_MS)
    {
        return;
    }
    if(g_raw_q_count > 0U)
    {
        return;
    }

    g_last_inject_ms = now;
    stage = MouseBridge_CurrentStage();
    dx = MouseBridge_ConsumeModifyStep(stage->dx_x10, &g_dx_accum_x10);
    dy = MouseBridge_ConsumeModifyStep(stage->dy_x10, &g_dy_accum_x10);
    if(dx == 0 && dy == 0)
    {
        return;
    }
    MouseBridge_TrackSessionInject(dx, dy);
    MouseBridge_AccumMotion(dx, dy, 0U, 0, 0U);
}

void MouseBridge_OnTimer1ms(void)
{
    MouseBridge_TryFlushRaw();
    MouseBridge_TickRecoilLogic();
    MouseBridge_TryLbtnKeepalive();

    g_inject_div++;
    if(g_inject_div >= RECOIL_INJECT_INTERVAL_MS)
    {
        g_inject_div = 0;
        if(g_springback_active)
        {
            MouseBridge_TickSpringback();
        }
        else
        {
            MouseBridge_InjectRecoil();
        }
    }

    MouseBridge_TryFlushRaw();
    MouseBridge_TryFlush();
}

void MouseBridge_ForwardReport(const uint8_t *data, uint16_t len)
{
    uint8_t buttons;
    int16_t dx;
    int16_t dy;
    int8_t wheel;
    uint8_t parsed;

    if(data == 0 || len == 0)
    {
        return;
    }

    MouseBridge_TryForwardRaw(data, len);

    if(!g_cfg.enabled && !g_raw_debug)
    {
        return;
    }

    buttons = 0U;
    dx = 0;
    dy = 0;
    wheel = 0;
    parsed = MouseBridge_ExtractButtons(data, len, &buttons);
    if(g_raw_debug || !parsed)
    {
        parsed = MouseBridge_ExtractReport(data, len, &buttons, &dx, &dy, &wheel);
    }

    if(!parsed)
    {
        MouseBridge_LogRawReport(data, len, 0U, 0U, 0, 0, 0);
        BridgeDebug_LogDrop("parse", data, len);
        return;
    }

    MouseBridge_LogRawReport(data, len, 1U, buttons, dx, dy, wheel);
    if(g_cfg.enabled)
    {
        MouseBridge_HandleButtons(buttons);
    }
    else
    {
        g_current_buttons = buttons;
    }
    g_forward_buttons = buttons;
}

void MouseBridge_Poll(void)
{
    MouseBridge_TryFlushRaw();
    MouseBridge_TryFlush();
}
