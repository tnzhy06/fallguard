#include "bsp_audio.h"
#include "i2s.h"
#include <string.h>

static bsp_audio_data_t data;
static volatile uint8_t irq_error;
static uint8_t dma_active;

static void failed(uint32_t error)
{
    data.last_error = error;
    ++data.errors;
    data.state = BSP_AUDIO_ERROR;
}

void BSP_AUDIO_Init(void)
{
    memset(&data, 0, sizeof(data));
    data.state = BSP_AUDIO_STOPPED;
    irq_error = dma_active = 0U;
}

uint8_t BSP_AUDIO_PlayLoop(const int16_t *pcm, uint16_t words)
{
    HAL_StatusTypeDef result;

    if (dma_active || irq_error)
        return 0U;
    if ((pcm == NULL) || ((uintptr_t)pcm % sizeof(int16_t)) ||
        (words == 0U) || (words % 2U) ||
        (hi2s2.Init.Mode != I2S_MODE_MASTER_TX) ||
        (hi2s2.Init.Standard != I2S_STANDARD_PHILIPS) ||
        (hi2s2.Init.DataFormat != I2S_DATAFORMAT_16B) ||
        (hi2s2.Init.MCLKOutput != I2S_MCLKOUTPUT_DISABLE) ||
        (hi2s2.Init.AudioFreq != I2S_AUDIOFREQ_8K) ||
        (hi2s2.hdmatx == NULL) || (hi2s2.hdmatx->Init.Mode != DMA_CIRCULAR) ||
        (hi2s2.hdmatx->Init.Direction != DMA_MEMORY_TO_PERIPH) ||
        (hi2s2.hdmatx->Init.PeriphInc != DMA_PINC_DISABLE) ||
        (hi2s2.hdmatx->Init.MemInc != DMA_MINC_ENABLE) ||
        (hi2s2.hdmatx->Init.PeriphDataAlignment != DMA_PDATAALIGN_HALFWORD) ||
        (hi2s2.hdmatx->Init.MemDataAlignment != DMA_MDATAALIGN_HALFWORD))
    {
        failed(BSP_AUDIO_ERROR_CONFIG);
        return 0U;
    }

    /* HAL's parameter is non-const, but transmit DMA never writes this memory. */
    irq_error = 0U;
    dma_active = 1U;
    data.state = BSP_AUDIO_PLAYING;
    result = HAL_I2S_Transmit_DMA(&hi2s2, (uint16_t *)(uintptr_t)pcm, words);
    if (result != HAL_OK)
    {
        dma_active = 0U;
        failed(BSP_AUDIO_ERROR_START | HAL_I2S_GetError(&hi2s2));
        return 0U;
    }
    return 1U;
}

static uint32_t stop_dma(void)
{
    HAL_StatusTypeDef result = HAL_OK;

    if (dma_active)
    {
        /* This HAL operation can wait for the final frame. Never call it in an IRQ. */
        result = HAL_I2S_DMAStop(&hi2s2);
        dma_active = 0U;
    }
    return (result == HAL_OK) ? 0U : (BSP_AUDIO_ERROR_STOP | HAL_I2S_GetError(&hi2s2));
}

void BSP_AUDIO_Stop(void)
{
    uint32_t error = stop_dma();

    irq_error = 0U;
    if (error)
        failed(error);
    else
        data.state = BSP_AUDIO_STOPPED;
}

void BSP_AUDIO_Poll(void)
{
    if (irq_error)
    {
        uint32_t error = HAL_I2S_GetError(&hi2s2);
        error |= stop_dma();
        irq_error = 0U;
        failed(error);
    }
}

void HAL_I2S_ErrorCallback(I2S_HandleTypeDef *hi2s)
{
    /* Keep interrupt work bounded; stop and diagnostics run in the main loop. */
    if (hi2s == &hi2s2)
        irq_error = 1U;
}

const bsp_audio_data_t *BSP_AUDIO_GetData(void)
{
    return &data;
}
