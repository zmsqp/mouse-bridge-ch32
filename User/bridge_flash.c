#include "bridge_flash.h"
#include "ch32v20x_flash.h"
#include "string.h"

#define BRIDGE_FLASH_CRC_OFFSET  8U
#define BRIDGE_FLASH_V2_SIZE     16U
#define BRIDGE_FLASH_V3_SIZE     26U
#define BRIDGE_FLASH_V4_SIZE     28U
#define BRIDGE_FLASH_STAGE_SIZE   6U
#define BRIDGE_FLASH_V5_STAGES    4U
#define BRIDGE_FLASH_V5_SIZE      (BRIDGE_FLASH_V4_SIZE + (BRIDGE_FLASH_STAGE_SIZE * BRIDGE_FLASH_V5_STAGES))

static uint32_t BridgeFlash_RecordSize(uint16_t version)
{
    if(version == 1U || version == 2U)
    {
        return BRIDGE_FLASH_V2_SIZE;
    }
    if(version == 3U)
    {
        return BRIDGE_FLASH_V3_SIZE;
    }
    if(version == 4U)
    {
        return BRIDGE_FLASH_V4_SIZE;
    }
    if(version == 5U)
    {
        return BRIDGE_FLASH_V5_SIZE;
    }
    return sizeof(BridgeFlashRecord);
}

static uint16_t BridgeFlash_Crc16(const uint8_t *data, uint32_t len)
{
    uint16_t crc = 0xFFFFU;
    uint32_t i;
    uint8_t bit;

    for(i = 0; i < len; i++)
    {
        crc ^= (uint16_t)data[i] << 8;
        for(bit = 0; bit < 8; bit++)
        {
            if(crc & 0x8000U)
            {
                crc = (uint16_t)((crc << 1) ^ 0x1021U);
            }
            else
            {
                crc <<= 1;
            }
        }
    }

    return crc;
}

static void BridgeFlash_ApplyProfileDefaults(MouseBridgeConfig *cfg)
{
    uint8_t i;

    cfg->game_dpi = MOUSE_BRIDGE_DEFAULT_DPI;
    cfg->game_sens_x1000 = MOUSE_BRIDGE_DEFAULT_SENS_X1000;
    cfg->cal_dpi = MOUSE_BRIDGE_DEFAULT_DPI;
    cfg->cal_sens_x1000 = MOUSE_BRIDGE_DEFAULT_SENS_X1000;
    cfg->cal_dy_x10 = 8;
    cfg->recoil_springback = 1U;
    cfg->stage_count = 1U;
    for(i = 0; i < MOUSE_BRIDGE_PROFILE_STAGES; i++)
    {
        cfg->stages[i].duration_ms = 0U;
        cfg->stages[i].dx_x10 = 0;
        cfg->stages[i].dy_x10 = 0;
    }
    cfg->stages[0].duration_ms = 0U;
    cfg->stages[0].dx_x10 = cfg->modify_dx;
    cfg->stages[0].dy_x10 = cfg->modify_dy;
}

static void BridgeFlash_ApplyDefaults(MouseBridgeConfig *cfg)
{
    cfg->enabled = 1;
    cfg->hotkey_active = 0;
    cfg->monitor_stream = 0;
    cfg->modify_dx = 0;
    cfg->modify_dy = 8;
    cfg->hotkey_hold_ms = MOUSE_BRIDGE_DEFAULT_HOLD_MS;
    BridgeFlash_ApplyProfileDefaults(cfg);
}

static uint8_t BridgeFlash_RecordValid(const BridgeFlashRecord *rec, uint16_t *version)
{
    uint16_t crc;
    uint32_t record_size;

    if(rec->magic != BRIDGE_FLASH_MAGIC)
    {
        return 0;
    }
    if(rec->version != 1U && rec->version != 2U && rec->version != 3U &&
       rec->version != 4U && rec->version != 5U && rec->version != BRIDGE_FLASH_VERSION)
    {
        return 0;
    }

    record_size = BridgeFlash_RecordSize(rec->version);
    crc = BridgeFlash_Crc16((const uint8_t *)rec + BRIDGE_FLASH_CRC_OFFSET,
                            record_size - BRIDGE_FLASH_CRC_OFFSET);
    if(crc != rec->crc16)
    {
        return 0;
    }

    *version = rec->version;
    return 1U;
}

static void BridgeFlash_LoadProfile(MouseBridgeConfig *cfg, const BridgeFlashRecord *rec)
{
    if(rec->game_dpi >= 100U && rec->game_dpi <= 32000U)
    {
        cfg->game_dpi = rec->game_dpi;
    }
    if(rec->game_sens_x1000 >= 10U && rec->game_sens_x1000 <= 10000U)
    {
        cfg->game_sens_x1000 = rec->game_sens_x1000;
    }
    if(rec->cal_dpi >= 100U && rec->cal_dpi <= 32000U)
    {
        cfg->cal_dpi = rec->cal_dpi;
    }
    if(rec->cal_sens_x1000 >= 10U && rec->cal_sens_x1000 <= 10000U)
    {
        cfg->cal_sens_x1000 = rec->cal_sens_x1000;
    }
    if(rec->cal_dy_x10 >= -127 && rec->cal_dy_x10 <= 127)
    {
        cfg->cal_dy_x10 = rec->cal_dy_x10;
    }
}

static void BridgeFlash_NormalizeStages(MouseBridgeConfig *cfg)
{
    uint8_t i;

    if(cfg->stage_count == 0U || cfg->stage_count > MOUSE_BRIDGE_PROFILE_STAGES)
    {
        cfg->stage_count = 1U;
    }

    for(i = 0; i < cfg->stage_count; i++)
    {
        if(cfg->stages[i].dx_x10 > 127) { cfg->stages[i].dx_x10 = 127; }
        if(cfg->stages[i].dx_x10 < -127) { cfg->stages[i].dx_x10 = -127; }
        if(cfg->stages[i].dy_x10 > 127) { cfg->stages[i].dy_x10 = 127; }
        if(cfg->stages[i].dy_x10 < -127) { cfg->stages[i].dy_x10 = -127; }
    }

    if(cfg->stage_count == 1U &&
       cfg->stages[0].dx_x10 == 0 &&
       cfg->stages[0].dy_x10 == 0)
    {
        cfg->stages[0].duration_ms = 0U;
        cfg->stages[0].dx_x10 = cfg->modify_dx;
        cfg->stages[0].dy_x10 = cfg->modify_dy;
    }

    cfg->modify_dx = cfg->stages[0].dx_x10;
    cfg->modify_dy = cfg->stages[0].dy_x10;
}

void BridgeFlash_Load(MouseBridgeConfig *cfg)
{
    const BridgeFlashRecord *rec = (const BridgeFlashRecord *)BRIDGE_FLASH_PAGE_ADDR;
    uint16_t version = 0;

    BridgeFlash_ApplyDefaults(cfg);

    if(!BridgeFlash_RecordValid(rec, &version))
    {
        return;
    }

    cfg->enabled = rec->enabled ? 1U : 0U;
    cfg->modify_dx = rec->modify_dx;
    cfg->modify_dy = rec->modify_dy;
    if(version == 1U)
    {
        cfg->modify_dx = (int16_t)(rec->modify_dx * 10);
        cfg->modify_dy = (int16_t)(rec->modify_dy * 10);
    }
    if(rec->hotkey_hold_ms <= 3000U)
    {
        cfg->hotkey_hold_ms = rec->hotkey_hold_ms;
    }

    if(version >= 3U)
    {
        BridgeFlash_LoadProfile(cfg, rec);
    }
    if(version >= 4U)
    {
        cfg->recoil_springback = rec->recoil_springback ? 1U : 0U;
    }
    else
    {
        cfg->recoil_springback = 1U;
    }

    if(version >= 5U)
    {
        uint8_t i;
        uint8_t saved_stages = (version == 5U) ? 4U : MOUSE_BRIDGE_PROFILE_STAGES;
        cfg->stage_count = rec->stage_count;
        for(i = 0; i < saved_stages; i++)
        {
            cfg->stages[i] = rec->stages[i];
        }
    }
    else
    {
        cfg->stage_count = 1U;
        cfg->stages[0].duration_ms = 0U;
        cfg->stages[0].dx_x10 = cfg->modify_dx;
        cfg->stages[0].dy_x10 = cfg->modify_dy;
    }
    BridgeFlash_NormalizeStages(cfg);
}

uint8_t BridgeFlash_Save(const MouseBridgeConfig *cfg)
{
    BridgeFlashRecord rec;
    FLASH_Status st;
    uint32_t addr;

    memset(&rec, 0, sizeof(rec));
    rec.magic = BRIDGE_FLASH_MAGIC;
    rec.version = BRIDGE_FLASH_VERSION;
    rec.enabled = cfg->enabled ? 1U : 0U;
    rec.modify_dx = cfg->modify_dx;
    rec.modify_dy = cfg->modify_dy;
    rec.hotkey_hold_ms = cfg->hotkey_hold_ms;
    rec.game_dpi = cfg->game_dpi;
    rec.game_sens_x1000 = cfg->game_sens_x1000;
    rec.cal_dpi = cfg->cal_dpi;
    rec.cal_sens_x1000 = cfg->cal_sens_x1000;
    rec.cal_dy_x10 = cfg->cal_dy_x10;
    rec.recoil_springback = cfg->recoil_springback ? 1U : 0U;
    rec.stage_count = cfg->stage_count;
    memcpy(rec.stages, cfg->stages, sizeof(rec.stages));
    rec.crc16 = BridgeFlash_Crc16((const uint8_t *)&rec + BRIDGE_FLASH_CRC_OFFSET,
                                  sizeof(rec) - BRIDGE_FLASH_CRC_OFFSET);

    FLASH_Unlock();
    st = FLASH_ErasePage(BRIDGE_FLASH_PAGE_ADDR);
    if(st != FLASH_COMPLETE)
    {
        FLASH_Lock();
        return 0;
    }

    addr = BRIDGE_FLASH_PAGE_ADDR;
    st = FLASH_ProgramWord(addr, rec.magic);
    if(st != FLASH_COMPLETE) { FLASH_Lock(); return 0; }
    addr += 4U;
    st = FLASH_ProgramHalfWord(addr, rec.version);
    if(st != FLASH_COMPLETE) { FLASH_Lock(); return 0; }
    addr += 2U;
    st = FLASH_ProgramHalfWord(addr, rec.crc16);
    if(st != FLASH_COMPLETE) { FLASH_Lock(); return 0; }
    addr += 2U;
    st = FLASH_ProgramHalfWord(addr, rec.enabled);
    if(st != FLASH_COMPLETE) { FLASH_Lock(); return 0; }
    addr += 2U;
    st = FLASH_ProgramHalfWord(addr, (uint16_t)rec.modify_dx);
    if(st != FLASH_COMPLETE) { FLASH_Lock(); return 0; }
    addr += 2U;
    st = FLASH_ProgramHalfWord(addr, (uint16_t)rec.modify_dy);
    if(st != FLASH_COMPLETE) { FLASH_Lock(); return 0; }
    addr += 2U;
    st = FLASH_ProgramHalfWord(addr, rec.hotkey_hold_ms);
    if(st != FLASH_COMPLETE) { FLASH_Lock(); return 0; }
    addr += 2U;
    st = FLASH_ProgramHalfWord(addr, rec.game_dpi);
    if(st != FLASH_COMPLETE) { FLASH_Lock(); return 0; }
    addr += 2U;
    st = FLASH_ProgramHalfWord(addr, rec.game_sens_x1000);
    if(st != FLASH_COMPLETE) { FLASH_Lock(); return 0; }
    addr += 2U;
    st = FLASH_ProgramHalfWord(addr, rec.cal_dpi);
    if(st != FLASH_COMPLETE) { FLASH_Lock(); return 0; }
    addr += 2U;
    st = FLASH_ProgramHalfWord(addr, rec.cal_sens_x1000);
    if(st != FLASH_COMPLETE) { FLASH_Lock(); return 0; }
    addr += 2U;
    st = FLASH_ProgramHalfWord(addr, (uint16_t)rec.cal_dy_x10);
    if(st != FLASH_COMPLETE) { FLASH_Lock(); return 0; }
    addr += 2U;
    st = FLASH_ProgramHalfWord(addr,
                               (uint16_t)rec.recoil_springback |
                               ((uint16_t)rec.stage_count << 8));
    if(st != FLASH_COMPLETE) { FLASH_Lock(); return 0; }
    addr += 2U;
    {
        uint8_t i;
        for(i = 0; i < MOUSE_BRIDGE_PROFILE_STAGES; i++)
        {
            st = FLASH_ProgramHalfWord(addr, rec.stages[i].duration_ms);
            if(st != FLASH_COMPLETE) { FLASH_Lock(); return 0; }
            addr += 2U;
            st = FLASH_ProgramHalfWord(addr, (uint16_t)rec.stages[i].dx_x10);
            if(st != FLASH_COMPLETE) { FLASH_Lock(); return 0; }
            addr += 2U;
            st = FLASH_ProgramHalfWord(addr, (uint16_t)rec.stages[i].dy_x10);
            if(st != FLASH_COMPLETE) { FLASH_Lock(); return 0; }
            addr += 2U;
        }
    }
    FLASH_Lock();

    return (st == FLASH_COMPLETE) ? 1U : 0U;
}
