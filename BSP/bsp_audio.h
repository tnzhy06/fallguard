#ifndef BSP_AUDIO_H
#define BSP_AUDIO_H

#include <stdint.h>

/* Upper bits describe BSP failures; lower bits retain HAL_I2S_ERROR_* values. */
#define BSP_AUDIO_ERROR_CONFIG 0x80000000UL
#define BSP_AUDIO_ERROR_START  0x40000000UL
#define BSP_AUDIO_ERROR_STOP   0x20000000UL

typedef enum
{
    BSP_AUDIO_STOPPED,
    BSP_AUDIO_PLAYING,
    BSP_AUDIO_ERROR
} bsp_audio_state_t;

typedef struct
{
    bsp_audio_state_t state;
    uint32_t errors;
    uint32_t last_error;
} bsp_audio_data_t;

void BSP_AUDIO_Init(void);
/* Interleaved signed 16-bit L/R samples must remain valid until stopped.
   DMA reads the buffer only, so a const array in internal Flash is supported.
   words counts 16-bit values (both channels), not bytes or stereo frames. */
uint8_t BSP_AUDIO_PlayLoop(const int16_t *pcm, uint16_t words);
void BSP_AUDIO_Stop(void);
void BSP_AUDIO_Poll(void);
const bsp_audio_data_t *BSP_AUDIO_GetData(void);

#endif
