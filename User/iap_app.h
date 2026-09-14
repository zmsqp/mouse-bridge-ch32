#ifndef SBZFQ_IAP_APP_H
#define SBZFQ_IAP_APP_H

#include <stdint.h>

void IapApp_Init(void);
uint8_t IapApp_ConfirmRunning(void);
uint8_t IapApp_RequestBootloader(void);
void IapApp_Poll(void);

#endif /* SBZFQ_IAP_APP_H */
