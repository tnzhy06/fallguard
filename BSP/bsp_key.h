#ifndef BSP_KEY_H
#define BSP_KEY_H

#include <stdint.h>

#define BSP_KEY1_PRESSED (1U << 0)
#define BSP_KEY2_PRESSED (1U << 1)

void BSP_KEY_Init(void);
uint8_t BSP_KEY_Scan(void);

#endif
