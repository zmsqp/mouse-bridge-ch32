#include "bridge_uart_cmd.h"
#include "bridge_flash.h"
#include "mouse_bridge.h"
#include "ch32v20x.h"
#include "stdio.h"
#include "string.h"
#include "stdlib.h"

#define BRIDGE_CMD_LINE_MAX   640

static char g_cmd_line[BRIDGE_CMD_LINE_MAX];
static uint16_t g_cmd_len;

static void BridgeUart_EnableRx(void)
{
    GPIO_InitTypeDef gpio = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    /* PA10 = USART1_RX */
    gpio.GPIO_Pin = GPIO_Pin_10;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &gpio);

    USART1->CTLR1 |= USART_Mode_Rx;
}

void BridgeUart_Init(uint32_t baudrate)
{
    (void)baudrate;
    g_cmd_len = 0;
    BridgeUart_EnableRx();
}

static void BridgeUart_SendLine(const char *text)
{
    printf("%s\r\n", text);
}

static void BridgeUart_SendStatus(void)
{
    MouseBridgeConfig *cfg = MouseBridge_GetConfig();

    printf("@S,%u,%u,%u,%d,%d,%u,%u\r\n",
           (unsigned)cfg->enabled,
           (unsigned)cfg->hotkey_active,
           (unsigned)cfg->aim_active,
           (int)cfg->modify_dx,
           (int)cfg->modify_dy,
           (unsigned)cfg->hotkey_hold_ms,
           (unsigned)cfg->recoil_springback);
}

static void BridgeUart_SendStages(void)
{
    MouseBridgeConfig *cfg = MouseBridge_GetConfig();
    uint8_t i;

    printf("@P2,%u", (unsigned)cfg->stage_count);
    for(i = 0; i < cfg->stage_count && i < MOUSE_BRIDGE_PROFILE_STAGES; i++)
    {
        printf(",%u,%d,%d",
               (unsigned)cfg->stages[i].duration_ms,
               (int)cfg->stages[i].dx_x100,
               (int)cfg->stages[i].dy_x100);
    }
    printf("\r\n");
}

static int16_t BridgeUart_ParseInt(const char *text)
{
    return (int16_t)atoi(text);
}

static int16_t BridgeUart_ClampStageX100(int16_t value)
{
    if(value > MOUSE_BRIDGE_STAGE_AXIS_MAX_X100)
    {
        return MOUSE_BRIDGE_STAGE_AXIS_MAX_X100;
    }
    if(value < -MOUSE_BRIDGE_STAGE_AXIS_MAX_X100)
    {
        return -MOUSE_BRIDGE_STAGE_AXIS_MAX_X100;
    }
    return value;
}

static int16_t BridgeUart_RoundX100ToX10(int16_t value)
{
    if(value >= 0)
    {
        return (int16_t)((value + 5) / 10);
    }
    return (int16_t)((value - 5) / 10);
}

static void BridgeUart_HandleCommand(char *line)
{
    char *cmd;
    char *arg1;
    char *arg2;
    char *arg3;
    char *arg4;
    MouseBridgeConfig *cfg = MouseBridge_GetConfig();

    cmd = strtok(line, " \t");
    if(cmd == 0)
    {
        return;
    }

    if(strcmp(cmd, "GET") == 0)
    {
        BridgeUart_SendStatus();
        BridgeUart_SendStages();
        return;
    }

    if(strcmp(cmd, "PING") == 0)
    {
        BridgeUart_SendLine("@OK,pong");
        return;
    }

    if(strcmp(cmd, "SET") == 0)
    {
        arg1 = strtok(0, " \t");
        arg2 = strtok(0, " \t");
        arg3 = strtok(0, " \t");
        arg4 = strtok(0, " \t");
        if(arg1 == 0 || arg2 == 0 || arg3 == 0)
        {
            BridgeUart_SendLine("@ERR,args");
            return;
        }

        cfg->modify_dx = BridgeUart_ParseInt(arg1);
        cfg->modify_dy = BridgeUart_ParseInt(arg2);
        if(cfg->modify_dx > 127) cfg->modify_dx = 127;
        if(cfg->modify_dx < -127) cfg->modify_dx = -127;
        if(cfg->modify_dy > 127) cfg->modify_dy = 127;
        if(cfg->modify_dy < -127) cfg->modify_dy = -127;
        cfg->hotkey_hold_ms = (uint16_t)BridgeUart_ParseInt(arg3);
        if(cfg->hotkey_hold_ms > 3000U)
        {
            cfg->hotkey_hold_ms = 3000U;
        }
        if(arg4 != 0)
        {
            cfg->recoil_springback = (arg4[0] == '1') ? 1U : 0U;
        }
        cfg->stage_count = 1U;
        cfg->stages[0].duration_ms = 0U;
        cfg->stages[0].dx_x100 = (int16_t)(cfg->modify_dx * 10);
        cfg->stages[0].dy_x100 = (int16_t)(cfg->modify_dy * 10);

        MouseBridge_OnParamsChanged();
        BridgeUart_SendLine("@OK,set");
        return;
    }

    if(strcmp(cmd, "STG") == 0 || strcmp(cmd, "ST2") == 0)
    {
        uint8_t count;
        uint8_t i;
        uint8_t precise = (strcmp(cmd, "ST2") == 0) ? 1U : 0U;
        MouseBridgeStage stages[MOUSE_BRIDGE_PROFILE_STAGES];

        arg1 = strtok(0, " \t");
        if(arg1 == 0)
        {
            BridgeUart_SendLine("@ERR,args");
            return;
        }
        count = (uint8_t)BridgeUart_ParseInt(arg1);
        if(count == 0U || count > MOUSE_BRIDGE_PROFILE_STAGES)
        {
            BridgeUart_SendLine("@ERR,count");
            return;
        }

        for(i = 0; i < count; i++)
        {
            char *dur = strtok(0, " \t");
            char *dx = strtok(0, " \t");
            char *dy = strtok(0, " \t");
            int16_t dur_ms;
            if(dur == 0 || dx == 0 || dy == 0)
            {
                BridgeUart_SendLine("@ERR,args");
                return;
            }
            dur_ms = BridgeUart_ParseInt(dur);
            if(dur_ms < 0) dur_ms = 0;
            if(dur_ms > 30000) dur_ms = 30000;
            stages[i].duration_ms = (uint16_t)dur_ms;
            if(precise)
            {
                stages[i].dx_x100 = BridgeUart_ClampStageX100(BridgeUart_ParseInt(dx));
                stages[i].dy_x100 = BridgeUart_ClampStageX100(BridgeUart_ParseInt(dy));
            }
            else
            {
                int16_t dx_x10 = BridgeUart_ParseInt(dx);
                int16_t dy_x10 = BridgeUart_ParseInt(dy);
                if(dx_x10 > 127) dx_x10 = 127;
                if(dx_x10 < -127) dx_x10 = -127;
                if(dy_x10 > 127) dy_x10 = 127;
                if(dy_x10 < -127) dy_x10 = -127;
                stages[i].dx_x100 = (int16_t)(dx_x10 * 10);
                stages[i].dy_x100 = (int16_t)(dy_x10 * 10);
            }
        }
        cfg->stage_count = count;
        for(i = 0; i < count; i++)
        {
            cfg->stages[i] = stages[i];
        }
        cfg->modify_dx = BridgeUart_RoundX100ToX10(cfg->stages[0].dx_x100);
        cfg->modify_dy = BridgeUart_RoundX100ToX10(cfg->stages[0].dy_x100);
        MouseBridge_OnParamsChanged();
        BridgeUart_SendLine(precise ? "@OK,st2" : "@OK,stg");
        return;
    }

    if(strcmp(cmd, "EN") == 0)
    {
        arg1 = strtok(0, " \t");
        cfg->enabled = (arg1 != 0 && arg1[0] == '1') ? 1U : 0U;
        MouseBridge_OnParamsChanged();
        BridgeUart_SendLine("@OK,en");
        return;
    }

    if(strcmp(cmd, "SAVE") == 0)
    {
        if(BridgeFlash_Save(cfg))
        {
            BridgeUart_SendLine("@OK,flash");
            BridgeUart_SendStatus();
            BridgeUart_SendStages();
        }
        else
        {
            BridgeUart_SendLine("@ERR,flash");
        }
        return;
    }

    if(strcmp(cmd, "MON") == 0)
    {
        arg1 = strtok(0, " \t");
        cfg->monitor_stream = (arg1 != 0 && arg1[0] == '1') ? 1U : 0U;
        BridgeUart_SendLine(cfg->monitor_stream ? "@OK,mon_on" : "@OK,mon_off");
        return;
    }

    if(strcmp(cmd, "RAW") == 0)
    {
        arg1 = strtok(0, " \t");
        MouseBridge_SetRawDebug((arg1 != 0 && arg1[0] == '1') ? 1U : 0U);
        BridgeUart_SendLine((arg1 != 0 && arg1[0] == '1') ? "@OK,raw_on" : "@OK,raw_off");
        return;
    }

    BridgeUart_SendLine("@ERR,cmd");
}

void BridgeUart_Poll(void)
{
    while(USART_GetFlagStatus(USART1, USART_FLAG_RXNE) != RESET)
    {
        char ch = (char)(USART_ReceiveData(USART1) & 0xFFU);

        if(ch == '\r')
        {
            continue;
        }

        if(ch == '\n')
        {
            if(g_cmd_len > 0U)
            {
                g_cmd_line[g_cmd_len] = '\0';
                BridgeUart_HandleCommand(g_cmd_line);
                g_cmd_len = 0;
            }
            continue;
        }

        if(g_cmd_len + 1U >= BRIDGE_CMD_LINE_MAX)
        {
            g_cmd_len = 0;
            continue;
        }

        g_cmd_line[g_cmd_len++] = ch;
    }
}
