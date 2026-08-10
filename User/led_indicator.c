#include "led_indicator.h"

#define LED_PORT    GPIOB
#define LED_PIN     GPIO_Pin_9

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

    tim.TIM_Period = 5000 - 1;
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

void TIM4_IRQHandler(void)
{
    if(TIM_GetITStatus(TIM4, TIM_IT_Update) == SET)
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
    TIM_ClearITPendingBit(TIM4, TIM_IT_Update);
}
