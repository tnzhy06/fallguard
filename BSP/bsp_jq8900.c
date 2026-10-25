#include "bsp_jq8900.h"
#include "main.h"
#include "tim.h"
#include <string.h>

typedef enum
{
    WAVE_START_HIGH,
    WAVE_GUIDE_LOW,
    WAVE_BIT_HIGH,
    WAVE_BIT_LOW
} wave_phase_t;

static volatile wave_phase_t wave_phase;
static volatile uint8_t tx_active, byte_done, tx_byte, tx_bit;
static uint8_t command[7], command_length, command_index;
static uint8_t stop_pending, gap_wait;
static uint32_t power_tick, byte_tick;
static bsp_jq8900_data_t data;

static void next_edge(uint16_t delay_us)
{
    __HAL_TIM_SET_AUTORELOAD(&htim3, (uint32_t)delay_us - 1U);
    __HAL_TIM_SET_COUNTER(&htim3, 0U);
    __HAL_TIM_CLEAR_FLAG(&htim3, TIM_FLAG_UPDATE);
}

static uint16_t high_time(void)
{
    return ((tx_byte >> tx_bit) & 1U) ? BSP_JQ8900_LONG_US : BSP_JQ8900_SHORT_US;
}

static uint16_t low_time(void)
{
    return ((tx_byte >> tx_bit) & 1U) ? BSP_JQ8900_SHORT_US : BSP_JQ8900_LONG_US;
}

uint8_t BSP_JQ8900_IsSending(void)
{
    return tx_active || byte_done || command_length || stop_pending || gap_wait;
}

static uint8_t queue_command(const uint8_t *bytes, uint8_t length)
{
    if (!data.ready || data.error || BSP_JQ8900_IsSending())
        return 0U;
    memcpy(command, bytes, length);
    command_length = length;
    command_index = 0U;
    return 1U;
}

void BSP_JQ8900_Init(void)
{
    HAL_TIM_Base_Stop_IT(&htim3);
    HAL_GPIO_WritePin(JQ_SDA_GPIO_Port, JQ_SDA_Pin, GPIO_PIN_SET);
    tx_active = byte_done = tx_byte = tx_bit = 0U;
    command_length = command_index = stop_pending = gap_wait = 0U;
    memset(&data, 0, sizeof(data));
    power_tick = HAL_GetTick();
}

void BSP_JQ8900_Poll(void)
{
    uint32_t now = HAL_GetTick();

    if (data.error)
        return;
    if (!data.ready)
    {
        if ((uint32_t)(now - power_tick) < BSP_JQ8900_POWER_WAIT_MS)
            return;
        data.ready = 1U;
    }
    /* Test active first: an IRQ completing now must be consumed before another byte starts. */
    if (tx_active)
        return;
    if (byte_done)
    {
        byte_done = 0U;
        ++data.bytes_sent;
        ++command_index;
        gap_wait = 1U;
        byte_tick = now;
        if (command_index == command_length)
        {
            command_length = command_index = 0U;
            ++data.commands_sent;
        }
    }
    if (stop_pending)
    {
        /* Replace unsent digits, but finish an active byte before sending STOP. */
        command[0] = 0x13U;
        command_length = 1U;
        command_index = 0U;
        stop_pending = 0U;
    }
    if (gap_wait)
    {
        if ((uint32_t)(now - byte_tick) < BSP_JQ8900_BYTE_GAP_MS)
            return;
        gap_wait = 0U;
    }
    if (command_length == 0U)
        return;
    tx_byte = command[command_index];
    tx_bit = 0U;
    wave_phase = WAVE_START_HIGH;
    byte_done = 0U;
    tx_active = 1U;
    HAL_GPIO_WritePin(JQ_SDA_GPIO_Port, JQ_SDA_Pin, GPIO_PIN_SET);
    next_edge(BSP_JQ8900_START_US);
    if (HAL_TIM_Base_Start_IT(&htim3) != HAL_OK)
    {
        HAL_TIM_Base_Stop_IT(&htim3);
        tx_active = 0U;
        command_length = 0U;
        data.error = 1U;
    }
}

void BSP_JQ8900_TimerIRQ(void)
{
    if (!tx_active)
        return;
    switch (wave_phase)
    {
    case WAVE_START_HIGH:
        HAL_GPIO_WritePin(JQ_SDA_GPIO_Port, JQ_SDA_Pin, GPIO_PIN_RESET);
        wave_phase = WAVE_GUIDE_LOW;
        next_edge(BSP_JQ8900_GUIDE_US);
        break;
    case WAVE_GUIDE_LOW:
        HAL_GPIO_WritePin(JQ_SDA_GPIO_Port, JQ_SDA_Pin, GPIO_PIN_SET);
        wave_phase = WAVE_BIT_HIGH;
        next_edge(high_time());
        break;
    case WAVE_BIT_HIGH:
        HAL_GPIO_WritePin(JQ_SDA_GPIO_Port, JQ_SDA_Pin, GPIO_PIN_RESET);
        wave_phase = WAVE_BIT_LOW;
        next_edge(low_time());
        break;
    default:
        ++tx_bit;
        HAL_GPIO_WritePin(JQ_SDA_GPIO_Port, JQ_SDA_Pin, GPIO_PIN_SET);
        if (tx_bit == 8U)
        {
            HAL_TIM_Base_Stop_IT(&htim3);
            tx_active = 0U;
            byte_done = 1U;
        }
        else
        {
            wave_phase = WAVE_BIT_HIGH;
            next_edge(high_time());
        }
        break;
    }
}

uint8_t BSP_JQ8900_SelectFlash(void)
{
    const uint8_t byte = 0x1AU;
    return queue_command(&byte, 1U);
}

uint8_t BSP_JQ8900_Play(uint32_t track)
{
    uint8_t bytes[7], digits[5], count = 0U;

    if ((track == 0U) || (track > 99999U))
        return 0U;
    do
    {
        digits[count++] = (uint8_t)(track % 10U);
        track /= 10U;
    } while (track != 0U);
    bytes[0] = 0x0AU;
    for (uint8_t i = 0U; i < count; ++i)
        bytes[i+1U] = digits[count-1U-i];
    bytes[count+1U] = 0x0BU;
    return queue_command(bytes, (uint8_t)(count + 2U));
}

uint8_t BSP_JQ8900_SetVolume(uint8_t volume)
{
    uint8_t bytes[4], length = 0U;

    if (volume > 30U)
        return 0U;
    bytes[length++] = 0x0AU;
    if (volume >= 10U)
        bytes[length++] = (uint8_t)(volume / 10U);
    bytes[length++] = (uint8_t)(volume % 10U);
    bytes[length++] = 0x0CU;
    return queue_command(bytes, length);
}

void BSP_JQ8900_Stop(void)
{
    if (!data.error)
        stop_pending = 1U;
}

uint8_t BSP_JQ8900_IsPlaying(void)
{
    return HAL_GPIO_ReadPin(JQ_BUSY_GPIO_Port, JQ_BUSY_Pin) == GPIO_PIN_SET;
}

const bsp_jq8900_data_t *BSP_JQ8900_GetData(void)
{
    return &data;
}
