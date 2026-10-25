#ifndef BSP_JQ8900_H
#define BSP_JQ8900_H

#include <stdint.h>

#define BSP_JQ8900_SHORT_US       500U
#define BSP_JQ8900_LONG_US        1500U
#define BSP_JQ8900_GUIDE_US       4000U
#define BSP_JQ8900_START_US       1000U
#define BSP_JQ8900_BYTE_GAP_MS    12U
#define BSP_JQ8900_POWER_WAIT_MS  1000U

typedef struct
{
    uint32_t bytes_sent, commands_sent;
    uint8_t ready, error;
} bsp_jq8900_data_t;

void BSP_JQ8900_Init(void);
void BSP_JQ8900_Poll(void);
void BSP_JQ8900_TimerIRQ(void);
uint8_t BSP_JQ8900_SelectFlash(void);
uint8_t BSP_JQ8900_Play(uint32_t track);
uint8_t BSP_JQ8900_SetVolume(uint8_t volume);
void BSP_JQ8900_Stop(void);
uint8_t BSP_JQ8900_IsSending(void);
uint8_t BSP_JQ8900_IsPlaying(void);
const bsp_jq8900_data_t *BSP_JQ8900_GetData(void);

#endif
