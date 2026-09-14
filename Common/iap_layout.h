#ifndef SBZFQ_IAP_LAYOUT_H
#define SBZFQ_IAP_LAYOUT_H

#include <stdint.h>

#define IAP_FLASH_PHYSICAL_BASE       0x08000000UL
#define IAP_BOOT_ALIAS_BASE           0x00000000UL
#define IAP_BOOT_PHYSICAL_BASE        0x08000000UL
#define IAP_BOOT_SIZE                 0x00003000UL

#define IAP_APP_HEADER_ALIAS_BASE     0x00003000UL
#define IAP_APP_HEADER_PHYSICAL_BASE  0x08003000UL
#define IAP_APP_HEADER_SIZE           0x00000100UL
#define IAP_APP_ALIAS_BASE            0x00003100UL
#define IAP_APP_PHYSICAL_BASE         0x08003100UL
#define IAP_APP_MAX_SIZE              0x0000BF00UL
#define IAP_APP_PARTITION_SIZE        0x0000C000UL
#define IAP_APP_PARTITION_END         0x0800EFFFUL

#define IAP_PROFILE_PHYSICAL_BASE     0x0800F000UL
#define IAP_PROFILE_SIZE              0x00001000UL
#define IAP_FLASH_PAGE_SIZE           0x00001000UL
#define IAP_APP_PAGE_COUNT            12U

#define IAP_RAM_BASE                  0x20000000UL
#define IAP_RAM_SIZE                  0x00005000UL
#define IAP_MAILBOX_SIZE              16UL
#define IAP_MAILBOX_ADDRESS           0x20004FF0UL
#define IAP_STACK_TOP                 IAP_MAILBOX_ADDRESS

#define IAP_MAILBOX_MAGIC             0x31415049UL /* "IAP1" */
#define IAP_MAILBOX_REASON_USB         0x00000001UL

#define IAP_IMAGE_HEADER_MAGIC        0x31474D49UL /* "IMG1" */
#define IAP_IMAGE_HEADER_VERSION      1U
#define IAP_IMAGE_STATE_EMPTY         0xFFFFFFFFUL
#define IAP_IMAGE_STATE_DOWNLOADING   0xFFFFFFFEUL
#define IAP_IMAGE_STATE_VALID         0xFFFFFFFCUL
#define IAP_IMAGE_STATE_BOOTING       0xFFFFFFF8UL
#define IAP_IMAGE_STATE_CONFIRMED     0xFFFFFFF0UL
#define IAP_IMAGE_STATE_FAILED        0xFFFFFFFAUL

#define IAP_APP_VERSION_CODE          0x00010000UL /* V1.00 */

typedef struct
{
    uint32_t magic;
    uint32_t magic_inverse;
    uint32_t reason;
    uint32_t reason_inverse;
} IapMailbox;

typedef struct __attribute__((packed, aligned(4)))
{
    uint32_t magic;
    uint16_t header_version;
    uint16_t header_size;
    uint32_t image_size;
    uint32_t image_crc32;
    uint32_t entry_address;
    uint32_t firmware_version;
    uint32_t build_id;
    uint32_t immutable_header_crc32;
    uint32_t error_code;
    uint32_t error_offset;
    uint32_t state_word;
    uint8_t reserved[212];
} IapImageHeader;

typedef char IapMailboxSizeMustBe16[(sizeof(IapMailbox) == IAP_MAILBOX_SIZE) ? 1 : -1];
typedef char IapImageHeaderSizeMustBe256[(sizeof(IapImageHeader) == IAP_APP_HEADER_SIZE) ? 1 : -1];

void IapMailbox_Clear(void);
void IapMailbox_Request(uint32_t reason);
uint8_t IapMailbox_Consume(uint32_t *reason);

#endif /* SBZFQ_IAP_LAYOUT_H */
