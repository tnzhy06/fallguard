#include "bsp_audio.h"
#include "app_config.h"
#include "i2s.h"
#include <string.h>

static bsp_audio_data_t data;
static volatile uint8_t irq_error;
static volatile uint8_t dma_active;

/* The default half holds 16 ms at 16 kHz. Only the consumed half is refilled.
 */
#define AUDIO_HALF_WORDS (AUDIO_HALF_FRAMES * 2U)
#define AUDIO_DMA_WORDS (AUDIO_HALF_WORDS * 2U)
static int16_t dma_buffer[AUDIO_DMA_WORDS] __attribute__((aligned(4)));
static const int16_t *source_pcm;
static uint32_t source_frames, source_index;
static uint32_t gap_frames, gap_remaining;

static void fill_half(uint32_t half) {
  int16_t *out = &dma_buffer[half * AUDIO_HALF_WORDS];
  uint32_t index = source_index;

  /* Bounded IRQ work: one configured half of mono samples, duplicated into L/R.
   */
  for (uint32_t i = 0U; i < AUDIO_HALF_FRAMES; ++i) {
    int16_t sample = 0;
    if (gap_remaining != 0U)
      --gap_remaining;
    else {
      sample = source_pcm[index];
      if (++index == source_frames) {
        index = 0U;
        gap_remaining = gap_frames;
      }
    }
    *out++ = sample;
    *out++ = sample;
  }
  source_index = index;
}

static void failed(uint32_t error) {
  data.last_error = error;
  ++data.errors;
  data.state = BSP_AUDIO_ERROR;
}

void BSP_AUDIO_Init(void) {
  memset(&data, 0, sizeof(data));
  data.state = BSP_AUDIO_STOPPED;
  irq_error = dma_active = 0U;
  source_pcm = NULL;
  source_frames = source_index = 0U;
  gap_frames = gap_remaining = 0U;
}

uint8_t BSP_AUDIO_PlayLoop(const int16_t *mono, uint32_t frames) {
  return BSP_AUDIO_PlayLoopWithGap(mono, frames, 0U);
}

uint8_t BSP_AUDIO_PlayLoopWithGap(const int16_t *mono, uint32_t frames,
                                  uint32_t gap_ms) {
  HAL_StatusTypeDef result;

  if (dma_active || irq_error)
    return 0U;
  if ((mono == NULL) || ((uintptr_t)mono % sizeof(int16_t)) || (frames == 0U) ||
      (gap_ms > UINT32_MAX / (I2S_AUDIOFREQ_16K / 1000U)) ||
      (hi2s2.Init.Mode != I2S_MODE_MASTER_TX) ||
      (hi2s2.Init.Standard != I2S_STANDARD_PHILIPS) ||
      (hi2s2.Init.DataFormat != I2S_DATAFORMAT_16B) ||
      (hi2s2.Init.MCLKOutput != I2S_MCLKOUTPUT_DISABLE) ||
      (hi2s2.Init.AudioFreq != I2S_AUDIOFREQ_16K) || (hi2s2.hdmatx == NULL) ||
      (hi2s2.hdmatx->Init.Mode != DMA_CIRCULAR) ||
      (hi2s2.hdmatx->Init.Direction != DMA_MEMORY_TO_PERIPH) ||
      (hi2s2.hdmatx->Init.PeriphInc != DMA_PINC_DISABLE) ||
      (hi2s2.hdmatx->Init.MemInc != DMA_MINC_ENABLE) ||
      (hi2s2.hdmatx->Init.PeriphDataAlignment != DMA_PDATAALIGN_HALFWORD) ||
      (hi2s2.hdmatx->Init.MemDataAlignment != DMA_MDATAALIGN_HALFWORD)) {
    failed(BSP_AUDIO_ERROR_CONFIG);
    return 0U;
  }

  source_pcm = mono;
  source_frames = frames;
  source_index = 0U;
  gap_frames = gap_ms * (I2S_AUDIOFREQ_16K / 1000U);
  gap_remaining = 0U;
  fill_half(0U);
  fill_half(1U);
  irq_error = 0U;
  dma_active = 1U;
  data.state = BSP_AUDIO_PLAYING;
  result =
      HAL_I2S_Transmit_DMA(&hi2s2, (uint16_t *)dma_buffer, AUDIO_DMA_WORDS);
  if (result != HAL_OK) {
    dma_active = 0U;
    source_pcm = NULL;
    source_frames = source_index = 0U;
    gap_frames = gap_remaining = 0U;
    failed(BSP_AUDIO_ERROR_START | HAL_I2S_GetError(&hi2s2));
    return 0U;
  }
  return 1U;
}

static uint32_t stop_dma(void) {
  HAL_StatusTypeDef result = HAL_OK;

  if (dma_active) {
    /* Ignore late callbacks before HAL starts disabling DMA/interrupts. */
    dma_active = 0U;
    /* This HAL operation can wait for the final frame. Never call it in an IRQ.
     */
    result = HAL_I2S_DMAStop(&hi2s2);
  }
  source_pcm = NULL;
  source_frames = source_index = 0U;
  gap_frames = gap_remaining = 0U;
  return (result == HAL_OK) ? 0U
                            : (BSP_AUDIO_ERROR_STOP | HAL_I2S_GetError(&hi2s2));
}

void BSP_AUDIO_Stop(void) {
  uint32_t error = stop_dma();

  irq_error = 0U;
  if (error)
    failed(error);
  else
    data.state = BSP_AUDIO_STOPPED;
}

void BSP_AUDIO_Poll(void) {
  if (irq_error) {
    uint32_t error = HAL_I2S_GetError(&hi2s2);
    error |= stop_dma();
    irq_error = 0U;
    failed(error);
  }
}

void HAL_I2S_TxHalfCpltCallback(I2S_HandleTypeDef *hi2s) {
  if ((hi2s == &hi2s2) && dma_active && !irq_error)
    fill_half(0U);
}

void HAL_I2S_TxCpltCallback(I2S_HandleTypeDef *hi2s) {
  if ((hi2s == &hi2s2) && dma_active && !irq_error)
    fill_half(1U);
}

void HAL_I2S_ErrorCallback(I2S_HandleTypeDef *hi2s) {
  /* Keep interrupt work bounded; stop and diagnostics run in the main loop. */
  if (hi2s == &hi2s2)
    irq_error = 1U;
}

const bsp_audio_data_t *BSP_AUDIO_GetData(void) { return &data; }
