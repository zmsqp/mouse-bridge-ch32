#include "bridge_usb_import.h"
#include "bridge_time.h"
#include "mouse_bridge.h"
#include "string.h"

#define BRIDGE_USB_IMPORT_PROTOCOL_VERSION  1U
#define BRIDGE_USB_IMPORT_WIRE_VERSION      1U
#define BRIDGE_USB_IMPORT_WIRE_HEADER       16U
#define BRIDGE_USB_IMPORT_WIRE_CRC           2U
#define BRIDGE_USB_IMPORT_WIRE_MAX          (BRIDGE_USB_IMPORT_WIRE_HEADER + \
                                             MOUSE_BRIDGE_PROFILE_STAGES * 6U + \
                                             BRIDGE_USB_IMPORT_WIRE_CRC)
#define BRIDGE_USB_IMPORT_TIMEOUT_MS        5000UL
#define BRIDGE_USB_IMPORT_ALL_PROFILES      ((1U << MOUSE_BRIDGE_PROFILE_COUNT) - 1U)
#define BRIDGE_USB_IMPORT_NO_PROFILE        0xFFU

/* Vendor usage page 0xFF00, one unnumbered 63-byte Feature Report. */
const uint8_t BridgeUsbImport_ReportDescriptor[BRIDGE_USB_IMPORT_REPORT_DESC_SIZE] = {
    0x06, 0x00, 0xFF,       /* Usage Page (Vendor 0xFF00) */
    0x09, 0x01,             /* Usage 1 */
    0xA1, 0x01,             /* Collection (Application) */
    0x15, 0x00,             /* Logical Minimum 0 */
    0x26, 0xFF, 0x00,       /* Logical Maximum 255 */
    0x75, 0x08,             /* Report Size 8 */
    0x95, 0x3F,             /* Report Count 63 */
    0x09, 0x01,             /* Usage 1 */
    0xB1, 0x02,             /* Feature (Data, Variable, Absolute) */
    0xC0                    /* End Collection */
};

static volatile uint8_t g_usb_import_pending;
static uint8_t g_usb_import_rx[BRIDGE_USB_IMPORT_PAYLOAD_SIZE];
static uint8_t g_usb_import_response[BRIDGE_USB_IMPORT_PAYLOAD_SIZE];
static MouseBridgeConfig g_usb_import_profiles[MOUSE_BRIDGE_PROFILE_COUNT];
static uint8_t g_usb_import_wire[BRIDGE_USB_IMPORT_WIRE_MAX];
static uint16_t g_usb_import_wire_total;
static uint16_t g_usb_import_wire_received;
static uint8_t g_usb_import_wire_profile;
static uint8_t g_usb_import_active;
static uint8_t g_usb_import_session;
static uint8_t g_usb_import_expected_seq;
static uint8_t g_usb_import_profile_mask;
static uint32_t g_usb_import_last_ms;

static uint16_t BridgeUsbImport_ReadU16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static int16_t BridgeUsbImport_ReadS16(const uint8_t *p)
{
    return (int16_t)BridgeUsbImport_ReadU16(p);
}

static void BridgeUsbImport_WriteU16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)(value & 0xFFU);
    p[1] = (uint8_t)(value >> 8);
}

static uint16_t BridgeUsbImport_Crc16(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFFU;
    uint16_t i;
    uint8_t bit;

    for(i = 0U; i < len; i++)
    {
        crc ^= (uint16_t)data[i] << 8;
        for(bit = 0U; bit < 8U; bit++)
        {
            crc = (crc & 0x8000U) ?
                (uint16_t)((crc << 1) ^ 0x1021U) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

static uint8_t BridgeUsbImport_MagicValid(const uint8_t *buf)
{
    return (buf[0] == 'S' && buf[1] == 'B' && buf[2] == 'U' && buf[3] == '1') ? 1U : 0U;
}

static uint8_t BridgeUsbImport_DataMatches(const uint8_t *buf, const char *text, uint8_t len)
{
    uint8_t i;
    if(buf[12] < len) return 0U;
    for(i = 0U; i < len; i++)
    {
        if(buf[13U + i] != (uint8_t)text[i]) return 0U;
    }
    return 1U;
}

static int16_t BridgeUsbImport_RoundX100ToX10(int16_t value)
{
    return (value >= 0) ? (int16_t)((value + 5) / 10) : (int16_t)((value - 5) / 10);
}

static void BridgeUsbImport_ResetSession(void)
{
    g_usb_import_active = 0U;
    g_usb_import_session = 0U;
    g_usb_import_expected_seq = 0U;
    g_usb_import_profile_mask = 0U;
    g_usb_import_wire_total = 0U;
    g_usb_import_wire_received = 0U;
    g_usb_import_wire_profile = BRIDGE_USB_IMPORT_NO_PROFILE;
}

static void BridgeUsbImport_SetResponse(uint8_t cmd, uint8_t session, uint8_t seq,
                                        uint8_t status, uint8_t detail)
{
    MouseBridgeConfig *cfg = MouseBridge_GetConfig();
    MouseBridgeLiveState live;
    uint8_t flags;
    uint16_t crc;

    MouseBridge_GetLiveState(&live);
    memset(g_usb_import_response, 0, sizeof(g_usb_import_response));
    g_usb_import_response[0] = 'S';
    g_usb_import_response[1] = 'B';
    g_usb_import_response[2] = 'U';
    g_usb_import_response[3] = '1';
    g_usb_import_response[4] = cmd;
    g_usb_import_response[5] = session;
    g_usb_import_response[6] = seq;
    g_usb_import_response[7] = status;
    g_usb_import_response[8] = g_usb_import_expected_seq;
    g_usb_import_response[9] = g_usb_import_profile_mask;
    g_usb_import_response[10] = g_usb_import_active;
    g_usb_import_response[11] = BRIDGE_USB_IMPORT_PROTOCOL_VERSION;
    g_usb_import_response[12] = live.selected_profile;
    g_usb_import_response[13] = live.profile_valid_mask;
    flags = (uint8_t)((cfg->enabled ? 0x01U : 0U) |
                      (cfg->monitor_stream ? 0x02U : 0U) |
                      (live.aim_active ? 0x04U : 0U) |
                      (live.recoil_active ? 0x08U : 0U) |
                      (cfg->recoil_springback ? 0x10U : 0U));
    g_usb_import_response[14] = flags;
    g_usb_import_response[15] = live.buttons;
    BridgeUsbImport_WriteU16(&g_usb_import_response[16], (uint16_t)cfg->modify_dx);
    BridgeUsbImport_WriteU16(&g_usb_import_response[18], (uint16_t)cfg->modify_dy);
    BridgeUsbImport_WriteU16(&g_usb_import_response[20], cfg->hotkey_hold_ms);
    BridgeUsbImport_WriteU16(&g_usb_import_response[22], cfg->game_dpi);
    BridgeUsbImport_WriteU16(&g_usb_import_response[24], cfg->game_sens_x1000);
    BridgeUsbImport_WriteU16(&g_usb_import_response[26], cfg->cal_dpi);
    BridgeUsbImport_WriteU16(&g_usb_import_response[28], cfg->cal_sens_x1000);
    BridgeUsbImport_WriteU16(&g_usb_import_response[30], (uint16_t)cfg->cal_dy_x10);
    g_usb_import_response[32] = detail;
    crc = BridgeUsbImport_Crc16(g_usb_import_response, 61U);
    BridgeUsbImport_WriteU16(&g_usb_import_response[61], crc);
}

static uint8_t BridgeUsbImport_ParseProfile(const uint8_t *wire, uint16_t len,
                                             MouseBridgeConfig *cfg)
{
    uint8_t stage_count;
    uint8_t i;
    uint16_t expected_len;
    uint16_t received_crc;

    if(len < BRIDGE_USB_IMPORT_WIRE_HEADER + BRIDGE_USB_IMPORT_WIRE_CRC ||
       wire[0] != BRIDGE_USB_IMPORT_WIRE_VERSION)
    {
        return 0U;
    }
    stage_count = wire[3];
    if(stage_count == 0U || stage_count > MOUSE_BRIDGE_PROFILE_STAGES)
    {
        return 0U;
    }
    expected_len = (uint16_t)(BRIDGE_USB_IMPORT_WIRE_HEADER +
                              (uint16_t)stage_count * 6U +
                              BRIDGE_USB_IMPORT_WIRE_CRC);
    if(len != expected_len)
    {
        return 0U;
    }
    received_crc = BridgeUsbImport_ReadU16(&wire[len - 2U]);
    if(received_crc != BridgeUsbImport_Crc16(wire, (uint16_t)(len - 2U)))
    {
        return 0U;
    }

    memset(cfg, 0, sizeof(*cfg));
    cfg->enabled = wire[1] ? 1U : 0U;
    cfg->recoil_springback = 1U;
    cfg->stage_count = stage_count;
    cfg->hotkey_hold_ms = BridgeUsbImport_ReadU16(&wire[4]);
    cfg->game_dpi = BridgeUsbImport_ReadU16(&wire[6]);
    cfg->game_sens_x1000 = BridgeUsbImport_ReadU16(&wire[8]);
    cfg->cal_dpi = BridgeUsbImport_ReadU16(&wire[10]);
    cfg->cal_sens_x1000 = BridgeUsbImport_ReadU16(&wire[12]);
    cfg->cal_dy_x10 = BridgeUsbImport_ReadS16(&wire[14]);
    if(cfg->hotkey_hold_ms > 3000U ||
       cfg->game_dpi < 100U || cfg->game_dpi > 32000U ||
       cfg->cal_dpi < 100U || cfg->cal_dpi > 32000U ||
       cfg->game_sens_x1000 < 10U || cfg->game_sens_x1000 > 10000U ||
       cfg->cal_sens_x1000 < 10U || cfg->cal_sens_x1000 > 10000U)
    {
        return 0U;
    }

    for(i = 0U; i < stage_count; i++)
    {
        uint16_t offset = (uint16_t)(BRIDGE_USB_IMPORT_WIRE_HEADER + (uint16_t)i * 6U);
        cfg->stages[i].duration_ms = BridgeUsbImport_ReadU16(&wire[offset]);
        cfg->stages[i].dx_x100 = BridgeUsbImport_ReadS16(&wire[offset + 2U]);
        cfg->stages[i].dy_x100 = BridgeUsbImport_ReadS16(&wire[offset + 4U]);
        if(cfg->stages[i].duration_ms > 30000U ||
           cfg->stages[i].dx_x100 < -1270 || cfg->stages[i].dx_x100 > 1270 ||
           cfg->stages[i].dy_x100 < -1270 || cfg->stages[i].dy_x100 > 1270)
        {
            return 0U;
        }
    }
    cfg->modify_dx = BridgeUsbImport_RoundX100ToX10(cfg->stages[0].dx_x100);
    cfg->modify_dy = BridgeUsbImport_RoundX100ToX10(cfg->stages[0].dy_x100);
    return 1U;
}

static void BridgeUsbImport_HandleSetActive(const uint8_t *buf)
{
    MouseBridgeConfig *cfg = MouseBridge_GetConfig();
    MouseBridgeConfig next = *cfg;
    const uint8_t *data = &buf[13];
    uint8_t flags;
    int16_t dx_x10;
    int16_t dy_x10;

    if(buf[12] != 17U)
    {
        BridgeUsbImport_SetResponse(buf[4], buf[5], buf[6], BRIDGE_USB_IMPORT_ERR_RANGE, 17U);
        return;
    }
    flags = data[0];
    dx_x10 = BridgeUsbImport_ReadS16(&data[1]);
    dy_x10 = BridgeUsbImport_ReadS16(&data[3]);
    if(dx_x10 < -127 || dx_x10 > 127 || dy_x10 < -127 || dy_x10 > 127)
    {
        BridgeUsbImport_SetResponse(buf[4], buf[5], buf[6], BRIDGE_USB_IMPORT_ERR_RANGE, 0U);
        return;
    }
    next.enabled = (flags & 0x01U) ? 1U : 0U;
    next.monitor_stream = (flags & 0x02U) ? 1U : 0U;
    next.recoil_springback = 1U;
    next.modify_dx = dx_x10;
    next.modify_dy = dy_x10;
    next.hotkey_hold_ms = BridgeUsbImport_ReadU16(&data[5]);
    next.game_dpi = BridgeUsbImport_ReadU16(&data[7]);
    next.game_sens_x1000 = BridgeUsbImport_ReadU16(&data[9]);
    next.cal_dpi = BridgeUsbImport_ReadU16(&data[11]);
    next.cal_sens_x1000 = BridgeUsbImport_ReadU16(&data[13]);
    next.cal_dy_x10 = BridgeUsbImport_ReadS16(&data[15]);
    if(next.hotkey_hold_ms > 3000U ||
       next.game_dpi < 100U || next.game_dpi > 32000U ||
       next.cal_dpi < 100U || next.cal_dpi > 32000U ||
       next.game_sens_x1000 < 10U || next.game_sens_x1000 > 10000U ||
       next.cal_sens_x1000 < 10U || next.cal_sens_x1000 > 10000U)
    {
        BridgeUsbImport_SetResponse(buf[4], buf[5], buf[6], BRIDGE_USB_IMPORT_ERR_RANGE, 0U);
        return;
    }
    next.stage_count = 1U;
    next.stages[0].duration_ms = 0U;
    next.stages[0].dx_x100 = (int16_t)(dx_x10 * 10);
    next.stages[0].dy_x100 = (int16_t)(dy_x10 * 10);
    *cfg = next;
    MouseBridge_OnParamsChanged();
    BridgeUsbImport_SetResponse(buf[4], buf[5], buf[6], BRIDGE_USB_IMPORT_STATUS_OK, 0U);
}

static void BridgeUsbImport_HandleData(const uint8_t *buf)
{
    uint8_t profile_id = buf[7];
    uint16_t offset = BridgeUsbImport_ReadU16(&buf[8]);
    uint16_t total = BridgeUsbImport_ReadU16(&buf[10]);
    uint8_t data_len = buf[12];

    if(!g_usb_import_active || buf[5] != g_usb_import_session)
    {
        BridgeUsbImport_SetResponse(buf[4], buf[5], buf[6], BRIDGE_USB_IMPORT_ERR_SESSION, 0U);
        return;
    }
    if(buf[6] != g_usb_import_expected_seq)
    {
        BridgeUsbImport_SetResponse(buf[4], buf[5], buf[6], BRIDGE_USB_IMPORT_ERR_SEQUENCE,
                                    g_usb_import_expected_seq);
        return;
    }
    if(profile_id >= MOUSE_BRIDGE_PROFILE_COUNT || data_len == 0U ||
       data_len > BRIDGE_USB_IMPORT_DATA_SIZE || total > BRIDGE_USB_IMPORT_WIRE_MAX ||
       total < BRIDGE_USB_IMPORT_WIRE_HEADER + BRIDGE_USB_IMPORT_WIRE_CRC ||
       offset > total || (uint32_t)offset + data_len > total)
    {
        BridgeUsbImport_SetResponse(buf[4], buf[5], buf[6], BRIDGE_USB_IMPORT_ERR_RANGE, profile_id);
        return;
    }

    if(offset == 0U)
    {
        if(g_usb_import_wire_profile != BRIDGE_USB_IMPORT_NO_PROFILE)
        {
            BridgeUsbImport_SetResponse(buf[4], buf[5], buf[6], BRIDGE_USB_IMPORT_ERR_INCOMPLETE,
                                        g_usb_import_wire_profile);
            return;
        }
        g_usb_import_wire_profile = profile_id;
        g_usb_import_wire_total = total;
        g_usb_import_wire_received = 0U;
    }
    if(g_usb_import_wire_profile != profile_id || g_usb_import_wire_total != total ||
       g_usb_import_wire_received != offset)
    {
        BridgeUsbImport_SetResponse(buf[4], buf[5], buf[6], BRIDGE_USB_IMPORT_ERR_SEQUENCE,
                                    (uint8_t)(g_usb_import_wire_received & 0xFFU));
        return;
    }

    memcpy(&g_usb_import_wire[offset], &buf[13], data_len);
    g_usb_import_wire_received = (uint16_t)(g_usb_import_wire_received + data_len);
    g_usb_import_expected_seq++;
    g_usb_import_last_ms = BridgeTime_GetMs();

    if(g_usb_import_wire_received == g_usb_import_wire_total)
    {
        if(!BridgeUsbImport_ParseProfile(g_usb_import_wire, g_usb_import_wire_total,
                                         &g_usb_import_profiles[profile_id]))
        {
            BridgeUsbImport_SetResponse(buf[4], buf[5], buf[6], BRIDGE_USB_IMPORT_ERR_PROFILE,
                                        profile_id);
            return;
        }
        g_usb_import_profile_mask |= (uint8_t)(1U << profile_id);
        g_usb_import_wire_profile = BRIDGE_USB_IMPORT_NO_PROFILE;
        g_usb_import_wire_total = 0U;
        g_usb_import_wire_received = 0U;
        BridgeUsbImport_SetResponse(buf[4], buf[5], buf[6], BRIDGE_USB_IMPORT_STATUS_PROFILE_OK,
                                    profile_id);
    }
    else
    {
        BridgeUsbImport_SetResponse(buf[4], buf[5], buf[6], BRIDGE_USB_IMPORT_STATUS_OK, profile_id);
    }
}

static void BridgeUsbImport_HandlePacket(const uint8_t *buf)
{
    uint8_t cmd;
    uint16_t received_crc;
    uint8_t i;

    cmd = buf[4];
    if(!BridgeUsbImport_MagicValid(buf))
    {
        BridgeUsbImport_SetResponse(cmd, buf[5], buf[6], BRIDGE_USB_IMPORT_ERR_PACKET, 0U);
        return;
    }
    received_crc = BridgeUsbImport_ReadU16(&buf[61]);
    if(received_crc != BridgeUsbImport_Crc16(buf, 61U))
    {
        BridgeUsbImport_SetResponse(cmd, buf[5], buf[6], BRIDGE_USB_IMPORT_ERR_CRC, 0U);
        return;
    }
    if(buf[12] > BRIDGE_USB_IMPORT_DATA_SIZE)
    {
        BridgeUsbImport_SetResponse(cmd, buf[5], buf[6], BRIDGE_USB_IMPORT_ERR_RANGE, buf[12]);
        return;
    }

    switch(cmd)
    {
        case BRIDGE_USB_IMPORT_CMD_PING:
        case BRIDGE_USB_IMPORT_CMD_GET_STATUS:
            BridgeUsbImport_SetResponse(cmd, buf[5], buf[6], BRIDGE_USB_IMPORT_STATUS_OK, 0U);
            break;

        case BRIDGE_USB_IMPORT_CMD_BEGIN:
            if(buf[5] == 0U || buf[6] != 0U ||
               !BridgeUsbImport_DataMatches(buf, "JSONBANK", 8U) ||
               buf[12] < 10U || buf[21] != BRIDGE_USB_IMPORT_WIRE_VERSION ||
               buf[22] != MOUSE_BRIDGE_PROFILE_COUNT)
            {
                BridgeUsbImport_SetResponse(cmd, buf[5], buf[6], BRIDGE_USB_IMPORT_ERR_PACKET, 0U);
                break;
            }
            BridgeUsbImport_ResetSession();
            memset(g_usb_import_profiles, 0, sizeof(g_usb_import_profiles));
            g_usb_import_active = 1U;
            g_usb_import_session = buf[5];
            g_usb_import_expected_seq = 1U;
            g_usb_import_last_ms = BridgeTime_GetMs();
            BridgeUsbImport_SetResponse(cmd, buf[5], buf[6], BRIDGE_USB_IMPORT_STATUS_READY, 0U);
            break;

        case BRIDGE_USB_IMPORT_CMD_DATA:
            BridgeUsbImport_HandleData(buf);
            break;

        case BRIDGE_USB_IMPORT_CMD_COMMIT:
            if(!g_usb_import_active || buf[5] != g_usb_import_session)
            {
                BridgeUsbImport_SetResponse(cmd, buf[5], buf[6], BRIDGE_USB_IMPORT_ERR_SESSION, 0U);
                break;
            }
            if(buf[6] != g_usb_import_expected_seq)
            {
                BridgeUsbImport_SetResponse(cmd, buf[5], buf[6], BRIDGE_USB_IMPORT_ERR_SEQUENCE,
                                            g_usb_import_expected_seq);
                break;
            }
            if(g_usb_import_wire_profile != BRIDGE_USB_IMPORT_NO_PROFILE ||
               g_usb_import_profile_mask != BRIDGE_USB_IMPORT_ALL_PROFILES ||
               !BridgeUsbImport_DataMatches(buf, "FLASHNOW", 8U))
            {
                BridgeUsbImport_SetResponse(cmd, buf[5], buf[6], BRIDGE_USB_IMPORT_ERR_INCOMPLETE,
                                            g_usb_import_profile_mask);
                break;
            }
            g_usb_import_expected_seq++;
            if(MouseBridge_ProfileCommitBank(g_usb_import_profiles))
            {
                g_usb_import_active = 0U;
                BridgeUsbImport_SetResponse(cmd, buf[5], buf[6], BRIDGE_USB_IMPORT_STATUS_FLASH_OK,
                                            g_usb_import_profile_mask);
            }
            else
            {
                BridgeUsbImport_SetResponse(cmd, buf[5], buf[6], BRIDGE_USB_IMPORT_ERR_FLASH, 0U);
            }
            break;

        case BRIDGE_USB_IMPORT_CMD_ABORT:
            BridgeUsbImport_ResetSession();
            BridgeUsbImport_SetResponse(cmd, buf[5], buf[6], BRIDGE_USB_IMPORT_STATUS_OK, 0U);
            break;

        case BRIDGE_USB_IMPORT_CMD_SET_ACTIVE:
            BridgeUsbImport_HandleSetActive(buf);
            break;

        case BRIDGE_USB_IMPORT_CMD_SAVE_ACTIVE:
            if(MouseBridge_GetProfileValidMask() != BRIDGE_USB_IMPORT_ALL_PROFILES)
            {
                MouseBridge_ProfileBegin();
                for(i = 0U; i < MOUSE_BRIDGE_PROFILE_COUNT; i++) MouseBridge_ProfileStore(i);
            }
            else
            {
                MouseBridge_ProfileStore(MouseBridge_GetSelectedProfile());
            }
            BridgeUsbImport_SetResponse(cmd, buf[5], buf[6],
                                        MouseBridge_ProfileCommit() ? BRIDGE_USB_IMPORT_STATUS_FLASH_OK :
                                                                      BRIDGE_USB_IMPORT_ERR_FLASH,
                                        0U);
            break;

        case BRIDGE_USB_IMPORT_CMD_MONITOR:
            if(buf[12] != 1U)
            {
                BridgeUsbImport_SetResponse(cmd, buf[5], buf[6], BRIDGE_USB_IMPORT_ERR_RANGE, 1U);
                break;
            }
            MouseBridge_GetConfig()->monitor_stream = buf[13] ? 1U : 0U;
            BridgeUsbImport_SetResponse(cmd, buf[5], buf[6], BRIDGE_USB_IMPORT_STATUS_OK, 0U);
            break;

        default:
            BridgeUsbImport_SetResponse(cmd, buf[5], buf[6], BRIDGE_USB_IMPORT_ERR_COMMAND, cmd);
            break;
    }
}

void BridgeUsbImport_Init(void)
{
    g_usb_import_pending = 0U;
    BridgeUsbImport_ResetSession();
    BridgeUsbImport_SetResponse(BRIDGE_USB_IMPORT_CMD_PING, 0U, 0U,
                                BRIDGE_USB_IMPORT_STATUS_OK, 0U);
}

void BridgeUsbImport_QueueFeatureReport(const uint8_t *buf, uint16_t len)
{
    if(buf == 0 || len != BRIDGE_USB_IMPORT_PAYLOAD_SIZE)
    {
        return;
    }
    if(g_usb_import_pending)
    {
        BridgeUsbImport_SetResponse(buf[4], buf[5], buf[6], BRIDGE_USB_IMPORT_ERR_BUSY, 0U);
        return;
    }
    memcpy(g_usb_import_rx, buf, BRIDGE_USB_IMPORT_PAYLOAD_SIZE);
    g_usb_import_pending = 1U;
}

void BridgeUsbImport_FillFeatureReport(uint8_t *buf, uint16_t len)
{
    if(buf == 0 || len < BRIDGE_USB_IMPORT_PAYLOAD_SIZE)
    {
        return;
    }
    memcpy(buf, g_usb_import_response, BRIDGE_USB_IMPORT_PAYLOAD_SIZE);
}

void BridgeUsbImport_Poll(void)
{
    if(g_usb_import_pending)
    {
        g_usb_import_pending = 0U;
        BridgeUsbImport_HandlePacket(g_usb_import_rx);
    }
    if(g_usb_import_active &&
       (uint32_t)(BridgeTime_GetMs() - g_usb_import_last_ms) > BRIDGE_USB_IMPORT_TIMEOUT_MS)
    {
        uint8_t old_session = g_usb_import_session;
        uint8_t old_seq = g_usb_import_expected_seq;
        BridgeUsbImport_ResetSession();
        BridgeUsbImport_SetResponse(BRIDGE_USB_IMPORT_CMD_ABORT, old_session, old_seq,
                                    BRIDGE_USB_IMPORT_ERR_TIMEOUT, 0U);
    }
}
