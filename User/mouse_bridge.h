#ifndef __MOUSE_BRIDGE_H
#define __MOUSE_BRIDGE_H

#include "ch32v20x.h"

#define MOUSE_BRIDGE_FIRMWARE_VERSION  "V0.99"

#define MOUSE_BRIDGE_REPORT_DESC_MAX   512
#define MOUSE_BRIDGE_REPORT_MAX          64

/* 侧键开启后，左键按住达到此延迟开始压枪；0 表示立刻开始 */
#define MOUSE_BRIDGE_DEFAULT_HOLD_MS     0U
#define MOUSE_BRIDGE_DEFAULT_DPI         800U
#define MOUSE_BRIDGE_DEFAULT_SENS_X1000  350U
#define MOUSE_BRIDGE_FLAG_SPRINGBACK     0x04U
#define MOUSE_BRIDGE_PROFILE_STAGES      30U
#define MOUSE_BRIDGE_STAGE_AXIS_MAX_X100 1270
#define MOUSE_BRIDGE_PROFILE_COUNT        6U

#define MOUSE_BRIDGE_GUN_AK               0U
#define MOUSE_BRIDGE_GUN_PHANTOM          1U
#define MOUSE_BRIDGE_GUN_ARES             2U
#define MOUSE_BRIDGE_GUN_ODIN             3U
#define MOUSE_BRIDGE_GUN_SPECTRE          4U
#define MOUSE_BRIDGE_GUN_BULLDOG          5U

typedef struct
{
    uint16_t duration_ms;
    int16_t  dx_x100;
    int16_t  dy_x100;
} MouseBridgeStage;

typedef struct
{
    uint8_t  enabled;
    uint8_t  hotkey_active;   /* 压枪进行中 */
    uint8_t  aim_active;      /* 压枪预备状态(鼠标前侧键切换) */
    uint8_t  monitor_stream;
    int16_t  modify_dx;       /* 单位 0.1 像素 */
    int16_t  modify_dy;
    uint16_t hotkey_hold_ms;  /* 左键启动延迟(ms)，侧键开启后生效 */
    uint16_t game_dpi;        /* 无畏契约：当前 DPI */
    uint16_t game_sens_x1000; /* 当前灵敏度 ×1000，0.35 → 350 */
    uint16_t cal_dpi;         /* 标定 DPI */
    uint16_t cal_sens_x1000;  /* 标定灵敏度 ×1000 */
    int16_t  cal_dy_x10;      /* 标定 Y 补偿(0.1px) */
    uint8_t  recoil_springback; /* 弹匣结束或松键后回到第一次开火的世界准星 */
    uint8_t  stage_count;
    MouseBridgeStage stages[MOUSE_BRIDGE_PROFILE_STAGES];
} MouseBridgeConfig;

typedef struct
{
    uint8_t aim_active;
    uint8_t recoil_active;
    uint8_t buttons;
    uint8_t selected_profile;
    uint8_t profile_valid_mask;
} MouseBridgeLiveState;

void MouseBridge_Init(void);
void MouseBridge_CloneDeviceDescriptor(const uint8_t *src);
void MouseBridge_SetReportDescriptor(const uint8_t *src, uint16_t len);
void MouseBridge_UseBootMouseLayout(void);
void MouseBridge_SetEndpointParams(uint16_t max_packet, uint8_t interval);
void MouseBridge_BuildConfigDescriptor(void);
void MouseBridge_OnHostReady(void);
void MouseBridge_ForwardReport(const uint8_t *data, uint16_t len);
void MouseBridge_Poll(void);
void MouseBridge_OnTimer1ms(void);
void MouseBridge_GetLiveState(MouseBridgeLiveState *state);
MouseBridgeConfig *MouseBridge_GetConfig(void);
void MouseBridge_OnParamsChanged(void);
void MouseBridge_SetRawDebug(uint8_t enabled);
void MouseBridge_ProfileBegin(void);
uint8_t MouseBridge_ProfileStore(uint8_t profile_id);
uint8_t MouseBridge_ProfileCommit(void);
uint8_t MouseBridge_ProfileCommitBank(const MouseBridgeConfig profiles[MOUSE_BRIDGE_PROFILE_COUNT]);
uint8_t MouseBridge_ProfileSelect(uint8_t profile_id);
uint8_t MouseBridge_GetSelectedProfile(void);
uint8_t MouseBridge_GetProfileValidMask(void);

#endif
