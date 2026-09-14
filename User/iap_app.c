#include "iap_app.h"
#include "iap_layout.h"
#include "bridge_time.h"
#include "ch32v20x.h"
#include <stddef.h>

#define IAP_APP_RESET_DELAY_MS 250UL

static uint8_t g_iap_reset_pending;
static uint32_t g_iap_reset_deadline;

void IapApp_Init(void)
{
    g_iap_reset_pending = 0U;
    g_iap_reset_deadline = 0U;
}

uint8_t IapApp_ConfirmRunning(void)
{
    const volatile uint32_t *state_word =
        (const volatile uint32_t *)(uintptr_t)(IAP_APP_HEADER_PHYSICAL_BASE +
                                               offsetof(IapImageHeader, state_word));
    FLASH_Status status;

    if(*state_word == IAP_IMAGE_STATE_CONFIRMED)
    {
        return 1U;
    }
    if(*state_word != IAP_IMAGE_STATE_BOOTING)
    {
        return 0U;
    }

    FLASH_Unlock();
    status = FLASH_ProgramHalfWord((uint32_t)(uintptr_t)state_word,
                                   (uint16_t)IAP_IMAGE_STATE_CONFIRMED);
    FLASH_Lock();
    return status == FLASH_COMPLETE && *state_word == IAP_IMAGE_STATE_CONFIRMED ? 1U : 0U;
}

uint8_t IapApp_RequestBootloader(void)
{
    if(g_iap_reset_pending)
    {
        return 0U;
    }

    IapMailbox_Request(IAP_MAILBOX_REASON_USB);
    g_iap_reset_deadline = BridgeTime_GetMs() + IAP_APP_RESET_DELAY_MS;
    g_iap_reset_pending = 1U;
    return 1U;
}

void IapApp_Poll(void)
{
    if(g_iap_reset_pending &&
       (int32_t)(BridgeTime_GetMs() - g_iap_reset_deadline) >= 0)
    {
        __disable_irq();
        __asm volatile("fence rw, rw" ::: "memory");
        NVIC_SystemReset();
        while(1)
        {
        }
    }
}
