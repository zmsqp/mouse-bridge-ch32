#ifndef SBZFQ_IAP_CRC_H
#define SBZFQ_IAP_CRC_H

#include <stdint.h>

uint16_t IapCrc16Ccitt(const uint8_t *data, uint32_t length);
uint32_t IapCrc32Begin(void);
uint32_t IapCrc32Update(uint32_t state, const uint8_t *data, uint32_t length);
uint32_t IapCrc32Finish(uint32_t state);
uint32_t IapCrc32(const uint8_t *data, uint32_t length);

#endif /* SBZFQ_IAP_CRC_H */
