#include "iap_crc.h"

uint16_t IapCrc16Ccitt(const uint8_t *data, uint32_t length)
{
    uint16_t crc = 0xFFFFU;
    uint32_t i;
    uint8_t bit;

    for(i = 0U; i < length; i++)
    {
        crc ^= (uint16_t)data[i] << 8;
        for(bit = 0U; bit < 8U; bit++)
        {
            crc = (crc & 0x8000U) ?
                (uint16_t)((crc << 1) ^ 0x1021U) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

uint32_t IapCrc32Begin(void)
{
    return 0xFFFFFFFFUL;
}

uint32_t IapCrc32Update(uint32_t state, const uint8_t *data, uint32_t length)
{
    uint32_t i;
    uint8_t bit;

    for(i = 0U; i < length; i++)
    {
        state ^= data[i];
        for(bit = 0U; bit < 8U; bit++)
        {
            state = (state & 1U) ? (state >> 1) ^ 0xEDB88320UL : state >> 1;
        }
    }
    return state;
}

uint32_t IapCrc32Finish(uint32_t state)
{
    return ~state;
}

uint32_t IapCrc32(const uint8_t *data, uint32_t length)
{
    return IapCrc32Finish(IapCrc32Update(IapCrc32Begin(), data, length));
}
