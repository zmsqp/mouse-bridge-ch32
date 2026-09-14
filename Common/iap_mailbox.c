#include "iap_layout.h"

static volatile IapMailbox *const g_iap_mailbox =
    (volatile IapMailbox *)(uintptr_t)IAP_MAILBOX_ADDRESS;

static void IapMailbox_Fence(void)
{
    __asm volatile("fence rw, rw" ::: "memory");
}

void IapMailbox_Clear(void)
{
    g_iap_mailbox->magic = 0U;
    g_iap_mailbox->magic_inverse = 0U;
    g_iap_mailbox->reason = 0U;
    g_iap_mailbox->reason_inverse = 0U;
    IapMailbox_Fence();
}

void IapMailbox_Request(uint32_t reason)
{
    IapMailbox_Clear();
    g_iap_mailbox->reason = reason;
    g_iap_mailbox->reason_inverse = ~reason;
    g_iap_mailbox->magic_inverse = ~IAP_MAILBOX_MAGIC;
    IapMailbox_Fence();
    g_iap_mailbox->magic = IAP_MAILBOX_MAGIC;
    IapMailbox_Fence();
}

uint8_t IapMailbox_Consume(uint32_t *reason)
{
    uint32_t magic = g_iap_mailbox->magic;
    uint32_t magic_inverse = g_iap_mailbox->magic_inverse;
    uint32_t stored_reason = g_iap_mailbox->reason;
    uint32_t reason_inverse = g_iap_mailbox->reason_inverse;
    uint8_t valid = (magic == IAP_MAILBOX_MAGIC &&
                     magic_inverse == ~IAP_MAILBOX_MAGIC &&
                     reason_inverse == ~stored_reason) ? 1U : 0U;

    IapMailbox_Clear();
    if(valid && reason != 0)
    {
        *reason = stored_reason;
    }
    return valid;
}
