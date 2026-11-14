#ifndef AUDIO_HOST_I2S_H
#define AUDIO_HOST_I2S_H
#include <stdint.h>

typedef enum { HAL_OK, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT } HAL_StatusTypeDef;
enum {
    I2S_MODE_MASTER_TX = 1, I2S_STANDARD_PHILIPS, I2S_DATAFORMAT_16B,
    I2S_MCLKOUTPUT_DISABLE, I2S_AUDIOFREQ_16K = 16000,
    DMA_CIRCULAR = 10, DMA_NORMAL, DMA_MEMORY_TO_PERIPH, DMA_PINC_DISABLE,
    DMA_MINC_ENABLE, DMA_PDATAALIGN_HALFWORD, DMA_MDATAALIGN_HALFWORD
};
typedef struct {
    uint32_t Mode, Direction, PeriphInc, MemInc, PeriphDataAlignment, MemDataAlignment;
} DMA_InitTypeDef;
typedef struct { DMA_InitTypeDef Init; } DMA_HandleTypeDef;
typedef struct {
    uint32_t Mode, Standard, DataFormat, MCLKOutput, AudioFreq;
} I2S_InitTypeDef;
typedef struct {
    I2S_InitTypeDef Init;
    DMA_HandleTypeDef *hdmatx;
} I2S_HandleTypeDef;
extern I2S_HandleTypeDef hi2s2;
HAL_StatusTypeDef HAL_I2S_Transmit_DMA(I2S_HandleTypeDef *, uint16_t *, uint16_t);
HAL_StatusTypeDef HAL_I2S_DMAStop(I2S_HandleTypeDef *);
uint32_t HAL_I2S_GetError(I2S_HandleTypeDef *);
void HAL_I2S_TxHalfCpltCallback(I2S_HandleTypeDef *);
void HAL_I2S_TxCpltCallback(I2S_HandleTypeDef *);
void HAL_I2S_ErrorCallback(I2S_HandleTypeDef *);
#endif
