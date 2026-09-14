#ifndef SBZFQ_IAP_PROTOCOL_H
#define SBZFQ_IAP_PROTOCOL_H

#include <stdint.h>

#define IAP_USB_VID                    0x1A86U
#define IAP_USB_APP_PID                0xFE01U
#define IAP_USB_BOOT_PID               0xFE02U
#define IAP_USB_USAGE_PAGE             0xFF00U
#define IAP_USB_USAGE                  0x0002U

#define IAP_USB_REPORT_ID              0U
#define IAP_USB_PAYLOAD_SIZE           63U
#define IAP_USB_HOST_REPORT_SIZE       64U
#define IAP_USB_DATA_SIZE              44U
#define IAP_USB_RESPONSE_DATA_SIZE     24U

#define IAP_CMD_PING                   0x01U
#define IAP_CMD_STATUS                 0x02U
#define IAP_CMD_BEGIN                  0x10U
#define IAP_CMD_DATA                   0x11U
#define IAP_CMD_END                    0x12U
#define IAP_CMD_ABORT                  0x13U
#define IAP_CMD_RESET                  0x14U

#define IAP_STATUS_OK                  0x00U
#define IAP_STATUS_ERASING             0x01U
#define IAP_STATUS_READY               0x02U
#define IAP_STATUS_DATA_ACK            0x03U
#define IAP_STATUS_VERIFYING           0x04U
#define IAP_STATUS_COMPLETE            0x05U

#define IAP_ERR_PACKET                 0x80U
#define IAP_ERR_CRC16                  0x81U
#define IAP_ERR_SESSION                0x82U
#define IAP_ERR_SEQUENCE               0x83U
#define IAP_ERR_RANGE                  0x84U
#define IAP_ERR_FLASH                  0x85U
#define IAP_ERR_IMAGE_CRC              0x86U
#define IAP_ERR_STATE                  0x87U
#define IAP_ERR_TIMEOUT                0x88U
#define IAP_ERR_BUSY                   0x89U
#define IAP_ERR_COMMAND                0x8AU
#define IAP_ERR_ABORTED                0x8BU

#define IAP_PHASE_WAIT                 0U
#define IAP_PHASE_ERASING              1U
#define IAP_PHASE_DOWNLOADING          2U
#define IAP_PHASE_VERIFYING            3U
#define IAP_PHASE_COMPLETE             4U
#define IAP_PHASE_FAILED               5U

#define IAP_PACKET_MAGIC_0             'S'
#define IAP_PACKET_MAGIC_1             'B'
#define IAP_PACKET_MAGIC_2             'I'
#define IAP_PACKET_MAGIC_3             '1'

#define IAP_BEGIN_METADATA_SIZE        16U
#define IAP_SESSION_TIMEOUT_MS         15000UL
#define IAP_COMPLETE_RESET_DELAY_MS    800UL

/* Response data inside payload[17..40]. */
#define IAP_RESP_STATUS_OFFSET         17U
#define IAP_RESP_PHASE_OFFSET          18U
#define IAP_RESP_ERASED_PAGES_OFFSET   19U
#define IAP_RESP_TOTAL_PAGES_OFFSET    20U
#define IAP_RESP_IMAGE_CRC_OFFSET      21U
#define IAP_RESP_ERROR_CODE_OFFSET     25U
#define IAP_RESP_ERROR_OFFSET_OFFSET   29U
#define IAP_RESP_VERSION_OFFSET        33U
#define IAP_RESP_UPTIME_OFFSET         37U

#endif /* SBZFQ_IAP_PROTOCOL_H */
