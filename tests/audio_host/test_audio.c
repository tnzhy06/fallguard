/* Compile the real BSP with a mocked HAL and emulate consumed DMA halves. */
#include "i2s.h"
#include "bsp_audio.h"
#include "app_audio.h"
#include "audio/alarm_pcm.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

I2S_HandleTypeDef hi2s2;
static DMA_HandleTypeDef dma;
static int16_t *output;
static uint16_t output_words;
static uint32_t error_code;
static unsigned starts, stops, in_callback, callbacks_on_stop;
static HAL_StatusTypeDef start_result, stop_result;
const int16_t app_alarm_pcm[APP_ALARM_PCM_FRAMES] = {1234, -1234};

HAL_StatusTypeDef HAL_I2S_Transmit_DMA(I2S_HandleTypeDef *i2s, uint16_t *p, uint16_t words)
{
    assert(i2s == &hi2s2 && !in_callback);
    ++starts;
    if (start_result != HAL_OK) return start_result;
    output = (int16_t *)p;
    output_words = words;
    assert(output && words && words % 4U == 0U);
    return HAL_OK;
}

HAL_StatusTypeDef HAL_I2S_DMAStop(I2S_HandleTypeDef *i2s)
{
    assert(i2s == &hi2s2 && !in_callback);
    ++stops;
    if (callbacks_on_stop)
    {
        int16_t *before = malloc(output_words * sizeof(int16_t));
        assert(before);
        memcpy(before, output, output_words * sizeof(int16_t));
        HAL_I2S_TxHalfCpltCallback(i2s);
        HAL_I2S_TxCpltCallback(i2s);
        assert(memcmp(before, output, output_words * sizeof(int16_t)) == 0);
        free(before);
    }
    return stop_result;
}

uint32_t HAL_I2S_GetError(I2S_HandleTypeDef *i2s)
{
    assert(i2s == &hi2s2);
    return error_code;
}

static void reset(void)
{
    memset(&hi2s2, 0, sizeof(hi2s2));
    memset(&dma, 0, sizeof(dma));
    hi2s2.Init.Mode = I2S_MODE_MASTER_TX;
    hi2s2.Init.Standard = I2S_STANDARD_PHILIPS;
    hi2s2.Init.DataFormat = I2S_DATAFORMAT_16B;
    hi2s2.Init.MCLKOutput = I2S_MCLKOUTPUT_DISABLE;
    hi2s2.Init.AudioFreq = I2S_AUDIOFREQ_16K;
    hi2s2.hdmatx = &dma;
    dma.Init.Mode = DMA_CIRCULAR;
    dma.Init.Direction = DMA_MEMORY_TO_PERIPH;
    dma.Init.PeriphInc = DMA_PINC_DISABLE;
    dma.Init.MemInc = DMA_MINC_ENABLE;
    dma.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
    dma.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
    starts = stops = in_callback = callbacks_on_stop = error_code = 0U;
    start_result = stop_result = HAL_OK;
    output = NULL; output_words = 0U;
    BSP_AUDIO_Init();
}

static void assert_callbacks_ignored(I2S_HandleTypeDef *handle)
{
    size_t bytes = output_words * sizeof(int16_t);
    int16_t *before = malloc(bytes); assert(before);
    memcpy(before, output, bytes);
    HAL_I2S_TxHalfCpltCallback(handle);
    HAL_I2S_TxCpltCallback(handle);
    assert(memcmp(before, output, bytes) == 0);
    free(before);
}

static void consume_with_gap(const int16_t *mono, uint32_t frames, uint32_t gap_ms, unsigned halves)
{
    uint64_t cycle = (uint64_t)frames + gap_ms * 16ULL;
    unsigned half_words = output_words / 2U;
    size_t half_bytes = half_words * sizeof(int16_t);
    int16_t *untouched = malloc(half_bytes); assert(untouched);
    assert(output != mono); /* HAL must only receive the small RAM buffer. */
    for (unsigned h = 0U; h < halves; ++h)
    {
        unsigned half = h % 2U;
        const int16_t *played = output + half * half_words;
        for (unsigned i = 0U; i < half_words; i += 2U)
        {
            uint64_t position = ((uint64_t)h * half_words / 2U + i / 2U) % cycle;
            int16_t expected = position < frames ? mono[position] : 0;
            assert(played[i] == expected);
            assert(played[i + 1U] == expected);
        }
        /* While filling the freed half, DMA is reading the opposite half. */
        int16_t *active = output + (1U - half) * half_words;
        memcpy(untouched, active, half_bytes);
        in_callback = 1U;
        if (half == 0U) HAL_I2S_TxHalfCpltCallback(&hi2s2);
        else HAL_I2S_TxCpltCallback(&hi2s2);
        in_callback = 0U;
        assert(memcmp(untouched, active, half_bytes) == 0);
        assert(BSP_AUDIO_GetData()->state == BSP_AUDIO_PLAYING);
    }
    free(untouched);
}

static void consume(const int16_t *mono, uint32_t frames, unsigned halves)
{
    consume_with_gap(mono, frames, 0U, halves);
}

static void test_repeat_gap_and_application_cancel(void)
{
    static int16_t mono[513];
    for (unsigned i = 0U; i < 513U; ++i)
        mono[i] = (int16_t)(i + 1U);
    const uint32_t lengths[] = {1U, 5U, 256U, 513U};
    const uint32_t gaps[] = {0U, 1U, 1000U};
    for (unsigned i = 0U; i < sizeof(lengths) / sizeof(lengths[0]); ++i)
    {
        for (unsigned j = 0U; j < sizeof(gaps) / sizeof(gaps[0]); ++j)
        {
            reset();
            assert(BSP_AUDIO_PlayLoopWithGap(mono, lengths[i], gaps[j]));
            consume_with_gap(mono, lengths[i], gaps[j], 160U);
            BSP_AUDIO_Stop();
            assert(BSP_AUDIO_PlayLoop(mono, lengths[i]));
            consume(mono, lengths[i], 2U); /* The legacy API clears a previous gap. */
            BSP_AUDIO_Stop();
        }
    }
    reset();
    assert(!BSP_AUDIO_PlayLoopWithGap(mono, 1U, UINT32_MAX));
    assert(BSP_AUDIO_GetData()->last_error == BSP_AUDIO_ERROR_CONFIG);
    assert(starts == 0U);

    reset();
    app_audio_init();
    app_audio_poll(1U);
    assert(starts == 1U && app_audio_get_state() == APP_AUDIO_PLAYING);
    consume_with_gap(app_alarm_pcm, APP_ALARM_PCM_FRAMES, APP_AUDIO_REPEAT_GAP_MS, 161U);
    app_audio_poll(1U);
    assert(starts == 1U); /* Continuous DMA across the silent interval. */
    callbacks_on_stop = 1U;
    app_audio_poll(0U); /* Cancel during the silent interval. */
    assert(stops == 1U && app_audio_get_state() == APP_AUDIO_IDLE);
    assert_callbacks_ignored(&hi2s2);
    app_audio_poll(1U);
    assert(starts == 2U);
    consume_with_gap(app_alarm_pcm, APP_ALARM_PCM_FRAMES, APP_AUDIO_REPEAT_GAP_MS, 2U);
    app_audio_poll(0U);
}

static void test_stream_and_stop(void)
{
    static const int16_t mono[] = {-32768, -1, 0, 1, 32767};
    reset();
    assert(BSP_AUDIO_PlayLoop(mono, 5U));
    assert(starts == 1U && output_words < 65535U);
    I2S_HandleTypeDef other = {0};
    assert_callbacks_ignored(&other);
    assert(!BSP_AUDIO_PlayLoop(mono, 5U)); /* Cannot replace an active source. */
    consume(mono, 5U, 60U); /* Many wraps of an odd, shorter-than-half source. */
    callbacks_on_stop = 1U;
    BSP_AUDIO_Stop();
    assert(stops == 1U && BSP_AUDIO_GetData()->state == BSP_AUDIO_STOPPED);
    assert_callbacks_ignored(&hi2s2);
    assert(BSP_AUDIO_PlayLoop(mono, 5U));
    consume(mono, 5U, 4U); /* Restart begins at the first sample. */
    BSP_AUDIO_Stop();
    assert(memcmp(mono, (int16_t[]){-32768, -1, 0, 1, 32767}, sizeof(mono)) == 0);
}

static void test_long_and_single_sample_sources(void)
{
    static int16_t long_mono[65537];
    for (uint32_t i = 0U; i < 65537U; ++i) long_mono[i] = (int16_t)((int)(i % 30001U) - 15000);
    reset();
    assert(BSP_AUDIO_PlayLoop(long_mono, 65537U));
    /* Stream past both the old uint16 DMA length and the end of the source. */
    consume(long_mono, 65537U, 600U);
    BSP_AUDIO_Stop();
    const int16_t one = -123;
    assert(BSP_AUDIO_PlayLoop(&one, 1U));
    consume(&one, 1U, 4U);
    BSP_AUDIO_Stop();
}

static void test_error_recovery(void)
{
    const int16_t mono[] = {1, -2, 3};
    reset();
    assert(BSP_AUDIO_PlayLoop(mono, 3U));
    I2S_HandleTypeDef other = {0};
    HAL_I2S_ErrorCallback(&other);
    BSP_AUDIO_Poll();
    assert(BSP_AUDIO_GetData()->errors == 0U && stops == 0U);
    error_code = 0x40U;
    HAL_I2S_ErrorCallback(&hi2s2);
    assert(stops == 0U); /* Never wait in an IRQ. */
    assert_callbacks_ignored(&hi2s2);
    BSP_AUDIO_Poll();
    assert(stops == 1U && BSP_AUDIO_GetData()->state == BSP_AUDIO_ERROR);
    assert(BSP_AUDIO_GetData()->last_error == 0x40U);
    BSP_AUDIO_Poll(); assert(BSP_AUDIO_GetData()->errors == 1U);
    BSP_AUDIO_Stop();
    assert(BSP_AUDIO_PlayLoop(mono, 3U));
    consume(mono, 3U, 4U);
    BSP_AUDIO_Stop();

    reset(); start_result = HAL_ERROR; error_code = 8U;
    assert(!BSP_AUDIO_PlayLoop(mono, 3U));
    assert(BSP_AUDIO_GetData()->last_error == (BSP_AUDIO_ERROR_START | 8U));
    start_result = HAL_OK;
    assert(BSP_AUDIO_PlayLoop(mono, 3U));
    consume(mono, 3U, 4U);
    stop_result = HAL_ERROR;
    BSP_AUDIO_Stop();
    assert(BSP_AUDIO_GetData()->state == BSP_AUDIO_ERROR);
    assert(BSP_AUDIO_GetData()->last_error & BSP_AUDIO_ERROR_STOP);
    assert_callbacks_ignored(&hi2s2);
}

static void test_invalid_configuration(void)
{
    const int16_t mono[] = {1, -1};
    reset(); assert(!BSP_AUDIO_PlayLoop(NULL, 2U));
    assert(!BSP_AUDIO_PlayLoop(mono, 0U));
    assert(!BSP_AUDIO_PlayLoop((const int16_t *)((const uint8_t *)mono + 1U), 1U));
    hi2s2.Init.AudioFreq = 8000U; assert(!BSP_AUDIO_PlayLoop(mono, 2U));
    hi2s2.Init.AudioFreq = I2S_AUDIOFREQ_16K;
    dma.Init.Mode = DMA_NORMAL; assert(!BSP_AUDIO_PlayLoop(mono, 2U));
    dma.Init.Mode = DMA_CIRCULAR;
    hi2s2.hdmatx = NULL; assert(!BSP_AUDIO_PlayLoop(mono, 2U));
    assert(starts == 0U && BSP_AUDIO_GetData()->last_error == BSP_AUDIO_ERROR_CONFIG);
}

int main(void)
{
    test_stream_and_stop();
    test_long_and_single_sample_sources();
    test_error_recovery();
    test_invalid_configuration();
    test_repeat_gap_and_application_cancel();
    puts("Audio streaming tests passed: mono/LR order, wrap, DMA ownership, stop/restart and faults.");
    return 0;
}
