#include "bridge_time.h"
#include "mouse_bridge.h"
#include "ch32v20x.h"

static volatile uint32_t g_ms;
static volatile uint8_t g_tick_pending;
static uint32_t g_serviced_ms;

void BridgeTime_Init(void)
{
    TIM_TimeBaseInitTypeDef tim = {0};
    NVIC_InitTypeDef nvic = {0};

    g_ms = 0;
    g_tick_pending = 0;
    g_serviced_ms = 0;

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);

    /* 1ms 时基；ISR 只做计数，业务逻辑在主循环 BridgeTime_Poll 执行 */
    tim.TIM_Period = 10U - 1U;
    tim.TIM_Prescaler = (uint16_t)(SystemCoreClock / 10000U - 1U);
    tim.TIM_ClockDivision = TIM_CKD_DIV1;
    tim.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM2, &tim);

    TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
    TIM_ITConfig(TIM2, TIM_IT_Update, ENABLE);

    nvic.NVIC_IRQChannel = TIM2_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 2;
    nvic.NVIC_IRQChannelSubPriority = 1;
    nvic.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic);

    TIM_Cmd(TIM2, ENABLE);
}

void TIM2_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void TIM2_IRQHandler(void)
{
    if(TIM_GetITStatus(TIM2, TIM_IT_Update) != RESET)
    {
        g_ms++;
        g_tick_pending = 1U;
        TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
    }
}

void BridgeTime_Poll(void)
{
    uint8_t budget = 4U;

    while(g_serviced_ms != g_ms && budget > 0U)
    {
        g_serviced_ms++;
        MouseBridge_OnTimer1ms();
        budget--;
    }

    g_tick_pending = (g_serviced_ms != g_ms) ? 1U : 0U;
}

uint32_t BridgeTime_GetMs(void)
{
    return g_ms;
}
