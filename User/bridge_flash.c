#include "bridge_flash.h"
#include "ch32v20x_flash.h"
#include "string.h"

#define BRIDGE_FLASH_CRC_OFFSET   8U
#define BRIDGE_FLASH_V2_SIZE     16U
#define BRIDGE_FLASH_V3_SIZE     26U
#define BRIDGE_FLASH_V4_SIZE     28U
#define BRIDGE_FLASH_STAGE_SIZE   6U
#define BRIDGE_FLASH_V5_STAGES    4U
#define BRIDGE_FLASH_V5_SIZE     (BRIDGE_FLASH_V4_SIZE + (BRIDGE_FLASH_STAGE_SIZE * BRIDGE_FLASH_V5_STAGES))
#define BRIDGE_FLASH_V6_STAGES    8U
#define BRIDGE_FLASH_V6_SIZE     (BRIDGE_FLASH_V4_SIZE + (BRIDGE_FLASH_STAGE_SIZE * BRIDGE_FLASH_V6_STAGES))
#define BRIDGE_FLASH_ALL_PROFILES ((1U << MOUSE_BRIDGE_PROFILE_COUNT) - 1U)

typedef struct __attribute__((packed))
{
    uint32_t magic;
    uint16_t version;
    uint16_t crc16;
    uint8_t valid_mask;
    uint8_t default_profile;
    uint16_t record_size;
    uint32_t generation;
    uint8_t reserved[16];
} BridgeFlashManifest;

typedef char BridgeFlashRecordMustFitSlot[
    (sizeof(BridgeFlashRecord) <= BRIDGE_FLASH_SLOT_SIZE) ? 1 : -1];
typedef char BridgeFlashManifestMustFitSlot[
    (sizeof(BridgeFlashManifest) <= BRIDGE_FLASH_SLOT_SIZE) ? 1 : -1];

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
            crc = (crc & 0x8000U) ? (uint16_t)((crc << 1) ^ 0x1021U) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

static uint32_t BridgeFlash_RecordSize(uint16_t version)
{
    if(version == 1U || version == 2U) return BRIDGE_FLASH_V2_SIZE;
    if(version == 3U) return BRIDGE_FLASH_V3_SIZE;
    if(version == 4U) return BRIDGE_FLASH_V4_SIZE;
    if(version == 5U) return BRIDGE_FLASH_V5_SIZE;
    if(version == 6U) return BRIDGE_FLASH_V6_SIZE;
    return sizeof(BridgeFlashRecord);
}

static int16_t BridgeFlash_RoundX100ToX10(int16_t value)
{
    return (value >= 0) ? (int16_t)((value + 5) / 10) : (int16_t)((value - 5) / 10);
}

static void BridgeFlash_ApplyDefaults(MouseBridgeConfig *cfg)
{
    uint8_t i;

    memset(cfg, 0, sizeof(*cfg));
    cfg->enabled = 1U;
    cfg->modify_dy = 8;
    cfg->hotkey_hold_ms = MOUSE_BRIDGE_DEFAULT_HOLD_MS;
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
        cfg->stages[i].dx_x100 = 0;
        cfg->stages[i].dy_x100 = 0;
    }
    cfg->stages[0].dy_x100 = 80;
}

static void BridgeFlash_Normalize(MouseBridgeConfig *cfg)
{
    uint8_t i;

    cfg->enabled = cfg->enabled ? 1U : 0U;
    cfg->hotkey_active = 0U;
    cfg->aim_active = 0U;
    cfg->monitor_stream = 0U;
    cfg->recoil_springback = 1U;
    if(cfg->hotkey_hold_ms > 3000U) cfg->hotkey_hold_ms = 3000U;
    if(cfg->stage_count == 0U || cfg->stage_count > MOUSE_BRIDGE_PROFILE_STAGES) cfg->stage_count = 1U;

    for(i = 0; i < cfg->stage_count; i++)
    {
        if(cfg->stages[i].dx_x100 > MOUSE_BRIDGE_STAGE_AXIS_MAX_X100) cfg->stages[i].dx_x100 = MOUSE_BRIDGE_STAGE_AXIS_MAX_X100;
        if(cfg->stages[i].dx_x100 < -MOUSE_BRIDGE_STAGE_AXIS_MAX_X100) cfg->stages[i].dx_x100 = -MOUSE_BRIDGE_STAGE_AXIS_MAX_X100;
        if(cfg->stages[i].dy_x100 > MOUSE_BRIDGE_STAGE_AXIS_MAX_X100) cfg->stages[i].dy_x100 = MOUSE_BRIDGE_STAGE_AXIS_MAX_X100;
        if(cfg->stages[i].dy_x100 < -MOUSE_BRIDGE_STAGE_AXIS_MAX_X100) cfg->stages[i].dy_x100 = -MOUSE_BRIDGE_STAGE_AXIS_MAX_X100;
    }
    cfg->modify_dx = BridgeFlash_RoundX100ToX10(cfg->stages[0].dx_x100);
    cfg->modify_dy = BridgeFlash_RoundX100ToX10(cfg->stages[0].dy_x100);
}

static uint8_t BridgeFlash_RecordValid(const BridgeFlashRecord *rec, uint16_t *version)
{
    uint16_t crc;
    uint32_t record_size;

    if(rec->magic != BRIDGE_FLASH_MAGIC) return 0U;
    if(rec->version < 1U || rec->version > BRIDGE_FLASH_VERSION) return 0U;
    record_size = BridgeFlash_RecordSize(rec->version);
    crc = BridgeFlash_Crc16((const uint8_t *)rec + BRIDGE_FLASH_CRC_OFFSET,
                            record_size - BRIDGE_FLASH_CRC_OFFSET);
    if(crc != rec->crc16) return 0U;
    *version = rec->version;
    return 1U;
}

static uint8_t BridgeFlash_ManifestValid(const BridgeFlashManifest *manifest)
{
    uint16_t crc;

    if(manifest->magic != BRIDGE_FLASH_BANK_MAGIC ||
       manifest->version != BRIDGE_FLASH_BANK_VERSION ||
       manifest->record_size != sizeof(BridgeFlashRecord) ||
       (manifest->valid_mask & (uint8_t)~BRIDGE_FLASH_ALL_PROFILES) != 0U ||
       manifest->default_profile >= MOUSE_BRIDGE_PROFILE_COUNT)
    {
        return 0U;
    }
    crc = BridgeFlash_Crc16((const uint8_t *)manifest + BRIDGE_FLASH_CRC_OFFSET,
                            sizeof(*manifest) - BRIDGE_FLASH_CRC_OFFSET);
    return (crc == manifest->crc16) ? 1U : 0U;
}

static void BridgeFlash_LoadRecord(MouseBridgeConfig *cfg, const BridgeFlashRecord *rec, uint16_t version)
{
    uint8_t i;
    uint8_t saved_stages = MOUSE_BRIDGE_PROFILE_STAGES;

    cfg->enabled = rec->enabled ? 1U : 0U;
    cfg->modify_dx = rec->modify_dx;
    cfg->modify_dy = rec->modify_dy;
    if(version == 1U)
    {
        cfg->modify_dx = (int16_t)(rec->modify_dx * 10);
        cfg->modify_dy = (int16_t)(rec->modify_dy * 10);
    }
    if(rec->hotkey_hold_ms <= 3000U) cfg->hotkey_hold_ms = rec->hotkey_hold_ms;

    if(version >= 3U)
    {
        if(rec->game_dpi >= 100U && rec->game_dpi <= 32000U) cfg->game_dpi = rec->game_dpi;
        if(rec->game_sens_x1000 >= 10U && rec->game_sens_x1000 <= 10000U) cfg->game_sens_x1000 = rec->game_sens_x1000;
        if(rec->cal_dpi >= 100U && rec->cal_dpi <= 32000U) cfg->cal_dpi = rec->cal_dpi;
        if(rec->cal_sens_x1000 >= 10U && rec->cal_sens_x1000 <= 10000U) cfg->cal_sens_x1000 = rec->cal_sens_x1000;
        if(rec->cal_dy_x10 >= -127 && rec->cal_dy_x10 <= 127) cfg->cal_dy_x10 = rec->cal_dy_x10;
    }

    if(version >= 5U)
    {
        if(version == 5U) saved_stages = BRIDGE_FLASH_V5_STAGES;
        else if(version == 6U) saved_stages = BRIDGE_FLASH_V6_STAGES;
        cfg->stage_count = rec->stage_count;
        if(cfg->stage_count > saved_stages) cfg->stage_count = saved_stages;
        for(i = 0; i < saved_stages; i++)
        {
            cfg->stages[i] = rec->stages[i];
            if(version < BRIDGE_FLASH_VERSION)
            {
                cfg->stages[i].dx_x100 = (int16_t)(cfg->stages[i].dx_x100 * 10);
                cfg->stages[i].dy_x100 = (int16_t)(cfg->stages[i].dy_x100 * 10);
            }
        }
    }
    else
    {
        cfg->stage_count = 1U;
        cfg->stages[0].duration_ms = 0U;
        cfg->stages[0].dx_x100 = (int16_t)(cfg->modify_dx * 10);
        cfg->stages[0].dy_x100 = (int16_t)(cfg->modify_dy * 10);
    }
    BridgeFlash_Normalize(cfg);
}

static void BridgeFlash_MakeRecord(BridgeFlashRecord *rec, const MouseBridgeConfig *cfg)
{
    memset(rec, 0, sizeof(*rec));
    rec->magic = BRIDGE_FLASH_MAGIC;
    rec->version = BRIDGE_FLASH_VERSION;
    rec->enabled = cfg->enabled ? 1U : 0U;
    rec->modify_dx = cfg->modify_dx;
    rec->modify_dy = cfg->modify_dy;
    rec->hotkey_hold_ms = cfg->hotkey_hold_ms;
    rec->game_dpi = cfg->game_dpi;
    rec->game_sens_x1000 = cfg->game_sens_x1000;
    rec->cal_dpi = cfg->cal_dpi;
    rec->cal_sens_x1000 = cfg->cal_sens_x1000;
    rec->cal_dy_x10 = cfg->cal_dy_x10;
    rec->recoil_springback = 1U;
    rec->stage_count = cfg->stage_count;
    memcpy(rec->stages, cfg->stages, sizeof(rec->stages));
    rec->crc16 = BridgeFlash_Crc16((const uint8_t *)rec + BRIDGE_FLASH_CRC_OFFSET,
                                   sizeof(*rec) - BRIDGE_FLASH_CRC_OFFSET);
}

static FLASH_Status BridgeFlash_ProgramBytes(uint32_t addr, const uint8_t *data, uint32_t len)
{
    FLASH_Status st = FLASH_COMPLETE;
    uint32_t i;

    for(i = 0; i < len; i += 2U)
    {
        uint16_t value = data[i];
        if(i + 1U < len) value |= (uint16_t)data[i + 1U] << 8;
        st = FLASH_ProgramHalfWord(addr + i, value);
        if(st != FLASH_COMPLETE) break;
    }
    return st;
}

void BridgeFlash_LoadBank(MouseBridgeConfig profiles[MOUSE_BRIDGE_PROFILE_COUNT],
                          uint8_t *valid_mask, uint8_t *default_profile)
{
    const BridgeFlashManifest *manifest = (const BridgeFlashManifest *)BRIDGE_FLASH_MANIFEST_ADDR;
    uint8_t i;
    uint8_t mask = 0U;

    for(i = 0; i < MOUSE_BRIDGE_PROFILE_COUNT; i++) BridgeFlash_ApplyDefaults(&profiles[i]);
    *valid_mask = 0U;
    *default_profile = MOUSE_BRIDGE_GUN_AK;

    if(BridgeFlash_ManifestValid(manifest))
    {
        for(i = 0; i < MOUSE_BRIDGE_PROFILE_COUNT; i++)
        {
            const BridgeFlashRecord *rec;
            uint16_t version = 0U;
            if((manifest->valid_mask & (uint8_t)(1U << i)) == 0U) continue;
            rec = (const BridgeFlashRecord *)(uintptr_t)BRIDGE_FLASH_PROFILE_ADDR(i);
            if(BridgeFlash_RecordValid(rec, &version))
            {
                BridgeFlash_LoadRecord(&profiles[i], rec, version);
                mask |= (uint8_t)(1U << i);
            }
        }
        *valid_mask = mask;
        if((mask & (uint8_t)(1U << manifest->default_profile)) != 0U)
        {
            *default_profile = manifest->default_profile;
        }
        return;
    }

    /* Upgrade path: the former single-profile record lived at 0x0800F000. */
    {
        const BridgeFlashRecord *legacy = (const BridgeFlashRecord *)BRIDGE_FLASH_BANK_ADDR;
        uint16_t legacy_version = 0U;
        if(BridgeFlash_RecordValid(legacy, &legacy_version))
        {
            BridgeFlash_LoadRecord(&profiles[MOUSE_BRIDGE_GUN_AK], legacy, legacy_version);
            *valid_mask = (uint8_t)(1U << MOUSE_BRIDGE_GUN_AK);
        }
    }
}

uint8_t BridgeFlash_SaveBank(const MouseBridgeConfig profiles[MOUSE_BRIDGE_PROFILE_COUNT],
                             uint8_t valid_mask, uint8_t default_profile)
{
    const BridgeFlashManifest *old_manifest = (const BridgeFlashManifest *)BRIDGE_FLASH_MANIFEST_ADDR;
    BridgeFlashManifest manifest;
    BridgeFlashRecord rec;
    FLASH_Status st;
    uint8_t i;

    if(valid_mask != BRIDGE_FLASH_ALL_PROFILES || default_profile >= MOUSE_BRIDGE_PROFILE_COUNT)
    {
        return 0U;
    }

    memset(&manifest, 0, sizeof(manifest));
    manifest.magic = BRIDGE_FLASH_BANK_MAGIC;
    manifest.version = BRIDGE_FLASH_BANK_VERSION;
    manifest.valid_mask = valid_mask;
    manifest.default_profile = default_profile;
    manifest.record_size = sizeof(BridgeFlashRecord);
    manifest.generation = BridgeFlash_ManifestValid(old_manifest) ? old_manifest->generation + 1U : 1U;
    manifest.crc16 = BridgeFlash_Crc16((const uint8_t *)&manifest + BRIDGE_FLASH_CRC_OFFSET,
                                      sizeof(manifest) - BRIDGE_FLASH_CRC_OFFSET);

    FLASH_Unlock();
    st = FLASH_ErasePage(BRIDGE_FLASH_BANK_ADDR);
    if(st != FLASH_COMPLETE)
    {
        FLASH_Lock();
        return 0U;
    }

    /* Records first and manifest last: an interrupted write never looks complete. */
    for(i = 0; i < MOUSE_BRIDGE_PROFILE_COUNT; i++)
    {
        BridgeFlash_MakeRecord(&rec, &profiles[i]);
        st = BridgeFlash_ProgramBytes(BRIDGE_FLASH_PROFILE_ADDR(i), (const uint8_t *)&rec, sizeof(rec));
        if(st != FLASH_COMPLETE)
        {
            FLASH_Lock();
            return 0U;
        }
    }
    st = BridgeFlash_ProgramBytes(BRIDGE_FLASH_MANIFEST_ADDR,
                                  (const uint8_t *)&manifest, sizeof(manifest));
    FLASH_Lock();
    if(st != FLASH_COMPLETE ||
       !BridgeFlash_ManifestValid((const BridgeFlashManifest *)BRIDGE_FLASH_MANIFEST_ADDR))
    {
        return 0U;
    }
    for(i = 0; i < MOUSE_BRIDGE_PROFILE_COUNT; i++)
    {
        const BridgeFlashRecord *saved =
            (const BridgeFlashRecord *)(uintptr_t)BRIDGE_FLASH_PROFILE_ADDR(i);
        uint16_t saved_version = 0U;
        if(!BridgeFlash_RecordValid(saved, &saved_version) ||
           saved_version != BRIDGE_FLASH_VERSION)
        {
            return 0U;
        }
    }
    return 1U;
}
