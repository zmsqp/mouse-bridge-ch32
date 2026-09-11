#ifndef __BRIDGE_FLASH_H
#define __BRIDGE_FLASH_H

#include "mouse_bridge.h"
#include <stdint.h>

/*
 * CH32V203C8T 64KB Flash layout:
 *   0x08000000..0x0800EFFF  firmware (sectors 0..14, 60KB)
 *   0x0800F000..0x0800FFFF  six-profile bank (sector 15, 4KB)
 * The bank contains a 512-byte manifest, six 512-byte fixed slots and 512
 * bytes reserved for a future journal.  One BANKSAVE erases sector 15 once.
 */
#define BRIDGE_FLASH_BANK_ADDR       0x0800F000UL
#define BRIDGE_FLASH_SECTOR_SIZE     0x1000UL
#define BRIDGE_FLASH_SLOT_SIZE       0x0200UL
#define BRIDGE_FLASH_MANIFEST_ADDR   BRIDGE_FLASH_BANK_ADDR
#define BRIDGE_FLASH_PROFILE_ADDR(id) (BRIDGE_FLASH_BANK_ADDR + BRIDGE_FLASH_SLOT_SIZE * ((uint32_t)(id) + 1U))
#define BRIDGE_FLASH_RESERVED_ADDR   0x0800FE00UL

/* Kept for source compatibility with the former one-profile implementation. */
#define BRIDGE_FLASH_PAGE_ADDR       BRIDGE_FLASH_BANK_ADDR
#define BRIDGE_FLASH_MAGIC           0x53424651UL  /* 'SBFQ' */
#define BRIDGE_FLASH_VERSION         7U
#define BRIDGE_FLASH_BANK_MAGIC      0x3642504DUL  /* 'MPB6' */
#define BRIDGE_FLASH_BANK_VERSION    1U

typedef struct __attribute__((packed))
{
    uint32_t magic;
    uint16_t version;
    uint16_t crc16;
    uint8_t  enabled;
    uint8_t  reserved;
    int16_t  modify_dx;
    int16_t  modify_dy;
    uint16_t hotkey_hold_ms;
    uint16_t game_dpi;
    uint16_t game_sens_x1000;
    uint16_t cal_dpi;
    uint16_t cal_sens_x1000;
    int16_t  cal_dy_x10;
    uint8_t  recoil_springback;
    uint8_t  stage_count;
    MouseBridgeStage stages[MOUSE_BRIDGE_PROFILE_STAGES];
} BridgeFlashRecord;

void BridgeFlash_LoadBank(MouseBridgeConfig profiles[MOUSE_BRIDGE_PROFILE_COUNT],
                          uint8_t *valid_mask, uint8_t *default_profile);
uint8_t BridgeFlash_SaveBank(const MouseBridgeConfig profiles[MOUSE_BRIDGE_PROFILE_COUNT],
                             uint8_t valid_mask, uint8_t default_profile);

#endif /* __BRIDGE_FLASH_H */
