#include "led_indicator.h"

#define LED_PORT    GPIOB
#define LED_PIN     GPIO_Pin_9
#define LED_TICK_MS 50U

static volatile LedIndicatorMode g_led_mode = LED_INDICATOR_OFF;
static volatile uint8_t g_selection_ticks;
static volatile uint8_t g_pattern_ticks;

void TIM4_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));

void LED_Indicator_Init(void)
{
    GPIO_InitTypeDef gpio = {0};
    TIM_TimeBaseInitTypeDef tim = {0};
    NVIC_InitTypeDef nvic = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM4, ENABLE);

    gpio.GPIO_Pin = LED_PIN;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(LED_PORT, &gpio);
    GPIO_ResetBits(LED_PORT, LED_PIN);

    tim.TIM_Period = (LED_TICK_MS * 10U) - 1U;
    tim.TIM_Prescaler = (uint16_t)(SystemCoreClock / 10000 - 1);
    tim.TIM_ClockDivision = TIM_CKD_DIV1;
    tim.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM4, &tim);

    TIM_ClearITPendingBit(TIM4, TIM_IT_Update);
    TIM_ITConfig(TIM4, TIM_IT_Update, ENABLE);

    nvic.NVIC_IRQChannel = TIM4_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 3;
    nvic.NVIC_IRQChannelSubPriority = 0;
    nvic.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic);

    TIM_Cmd(TIM4, ENABLE);
}

void LED_Indicator_SetMode(LedIndicatorMode mode)
{
    if(mode > LED_INDICATOR_FAST)
    {
        mode = LED_INDICATOR_OFF;
    }
    g_led_mode = mode;
    if(g_selection_ticks == 0U)
    {
        g_pattern_ticks = 0U;
    }
}

void LED_Indicator_PulseSelection(void)
{
    /* 1.2 秒慢闪用于确认切枪，之后自动回到开/关状态。 */
    g_selection_ticks = (uint8_t)(1200U / LED_TICK_MS);
    g_pattern_ticks = 0U;
}

void TIM4_IRQHandler(void)
{
    if(TIM_GetITStatus(TIM4, TIM_IT_Update) == SET)
    {
        LedIndicatorMode shown_mode = g_led_mode;

        if(g_selection_ticks > 0U)
        {
            shown_mode = LED_INDICATOR_SLOW;
            g_selection_ticks--;
        }

        g_pattern_ticks++;
        if(shown_mode == LED_INDICATOR_OFF)
        {
            GPIO_ResetBits(LED_PORT, LED_PIN);
        }
        else if(shown_mode == LED_INDICATOR_ON)
        {
            GPIO_SetBits(LED_PORT, LED_PIN);
        }
        else if((shown_mode == LED_INDICATOR_FAST && (g_pattern_ticks % 2U) == 0U) ||
                (shown_mode == LED_INDICATOR_SLOW && (g_pattern_ticks % 10U) == 0U))
        {
            if(GPIO_ReadOutputDataBit(LED_PORT, LED_PIN))
            {
                GPIO_ResetBits(LED_PORT, LED_PIN);
            }
            else
            {
                GPIO_SetBits(LED_PORT, LED_PIN);
            }
        }
    }
    TIM_ClearITPendingBit(TIM4, TIM_IT_Update);
}
