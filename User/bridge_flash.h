#ifndef __BRIDGE_FLASH_H
#define __BRIDGE_FLASH_H

#include "mouse_bridge.h"
#include <stdint.h>

/* CH32V203C8T 64KB Flash，最后一页 4KB 专用于桥接配置 */
#define BRIDGE_FLASH_PAGE_ADDR   0x0800F000UL
#define BRIDGE_FLASH_MAGIC       0x53424651UL  /* 'SBFQ' */
#define BRIDGE_FLASH_VERSION     7U

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

void BridgeFlash_Load(MouseBridgeConfig *cfg);
uint8_t BridgeFlash_Save(const MouseBridgeConfig *cfg);

#endif /* __BRIDGE_FLASH_H */
