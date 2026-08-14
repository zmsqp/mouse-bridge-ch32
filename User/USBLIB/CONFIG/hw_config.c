/********************************** (C) COPYRIGHT *******************************
 * File Name          : hw_config.c
 * Author             : WCH
 * Version            : V1.0.0
 * Date               : 2021/08/08
 * Description        : USB configuration file.
*********************************************************************************
* Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
* Attention: This software (modified or not) and binary are used for 
* microcontroller manufactured by Nanjing Qinheng Microelectronics.
*******************************************************************************/ 
#include "usb_lib.h"
#include "usb_prop.h"
#include "usb_desc.h"
#include "usb_istr.h"
#include "hw_config.h"
#include "usb_pwr.h"

void USBWakeUp_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void USB_LP_CAN1_RX0_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));

void USBWakeUp_IRQHandler(void) 
{
	EXTI_ClearITPendingBit(EXTI_Line18);
} 

void USB_LP_CAN1_RX0_IRQHandler(void) 
{
	USB_Istr();
} 

void Set_USBConfig( )
{
    RCC_ClocksTypeDef RCC_ClocksStatus={0};
    RCC_GetClocksFreq(&RCC_ClocksStatus);
    if( RCC_ClocksStatus.SYSCLK_Frequency == 144000000 )
    {
        RCC_USBCLKConfig( RCC_USBCLKSource_PLLCLK_Div3 );
    }
    else if( RCC_ClocksStatus.SYSCLK_Frequency == 96000000 ) 
    {
        RCC_USBCLKConfig( RCC_USBCLKSource_PLLCLK_Div2 );
    }
    else if( RCC_ClocksStatus.SYSCLK_Frequency == 48000000 ) 
    {
        RCC_USBCLKConfig( RCC_USBCLKSource_PLLCLK_Div1 );
    }
#if defined(CH32V20x_D8W) || defined(CH32V20x_D8)
    else if ( RCC_ClocksStatus.SYSCLK_Frequency == 240000000 && RCC_USB5PRE_JUDGE() == SET )
    {
        RCC_USBCLKConfig( RCC_USBCLKSource_PLLCLK_Div5 );
    }
#endif
    else
    {
        /* Fallback: assume 96MHz PLL for USB 48MHz */
        RCC_USBCLKConfig( RCC_USBCLKSource_PLLCLK_Div2 );
    }
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USB, ENABLE);
    /* USBFS Host 与 USBD 共享 48MHz USB 时钟，boot 时一次性配置，运行中不再改 */
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_USBFS, ENABLE);
}

void Enter_LowPowerMode(void)
{  
	bDeviceState = SUSPENDED;
	USBD_Sleep_Status |= 0x02;
} 

void Leave_LowPowerMode(void)
{
	DEVICE_INFO *pInfo=&Device_Info;
	USBD_Sleep_Status &= ~0x02;
	if (pInfo->Current_Configuration!=0)bDeviceState=CONFIGURED; 
	else bDeviceState = ATTACHED; 
} 

void USB_Interrupts_Config(void)
{ 
	NVIC_InitTypeDef NVIC_InitStructure;
	EXTI_InitTypeDef EXTI_InitStructure;

	EXTI_ClearITPendingBit(EXTI_Line18);
	EXTI_InitStructure.EXTI_Line = EXTI_Line18; 
	EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Event;
	EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Rising_Falling;	
	EXTI_InitStructure.EXTI_LineCmd = ENABLE;
	EXTI_Init(&EXTI_InitStructure); 	 
    
    EXTI->INTENR |= EXTI_INTENR_MR18;

	NVIC_InitStructure.NVIC_IRQChannel = USB_LP_CAN1_RX0_IRQn;	
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&NVIC_InitStructure);
}	

void USB_Port_Set(FunctionalState NewState, FunctionalState Pin_In_IPU)
{
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

	if(NewState) {
		_SetCNTR(_GetCNTR()&(~(1<<1)));
		GPIOA->CFGHR &= 0xFFF00FFF;
		GPIOA->OUTDR &= ~(3<<11);
		GPIOA->CFGHR |= 0x00044000;
	}
	else
	{	  
		_SetCNTR(_GetCNTR()|(1<<1));  
		GPIOA->CFGHR &= 0xFFF00FFF;
		GPIOA->OUTDR &= ~(3<<11);
		GPIOA->CFGHR |= 0x00033000;
	}
	
	if(Pin_In_IPU) (EXTEN->EXTEN_CTR) |= EXTEN_USBD_PU_EN; 
	else (EXTEN->EXTEN_CTR) &= ~EXTEN_USBD_PU_EN;
}
