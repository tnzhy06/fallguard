#include "bsp_log.h"
#include "usart.h"
#include <string.h>

_Static_assert(DEBUG_TX_BUFFER_SIZE > 1U && DEBUG_TX_BUFFER_SIZE <= UINT16_MAX,
               "Invalid log buffer size");
_Static_assert(DEBUG_TX_CHUNK_SIZE > 0U && DEBUG_TX_CHUNK_SIZE < DEBUG_TX_BUFFER_SIZE,
               "Invalid log transmit chunk size");

static uint8_t buffer[DEBUG_TX_BUFFER_SIZE];
static volatile uint16_t head, tail, tx_length;
static volatile uint8_t tx_error;
static uint32_t tx_tick;
static bsp_log_data_t data;

void BSP_LOG_Init(void)
{
    head = tail = tx_length = 0U;
    tx_error = 0U;
    tx_tick = HAL_GetTick();
    memset(&data, 0, sizeof(data));
}

void BSP_LOG_Write(const char *text, uint16_t length)
{
    uint32_t primask = __get_PRIMASK();
    uint16_t available, accepted, first;

    if (text == NULL || length == 0U)
        return;
    __disable_irq();
    /* In-flight bytes stay in the ring until TC, so producers cannot overwrite them. */
    available = (uint16_t)((tail + DEBUG_TX_BUFFER_SIZE - head - 1U) % DEBUG_TX_BUFFER_SIZE);
    accepted = length < available ? length : available;
    first = (uint16_t)(DEBUG_TX_BUFFER_SIZE - head);
    if (first > accepted)
        first = accepted;
    memcpy(&buffer[head], text, first);
    memcpy(buffer, text + first, accepted - first);
    head = (uint16_t)((head + accepted) % DEBUG_TX_BUFFER_SIZE);
    data.dropped_bytes += length - accepted;
    __set_PRIMASK(primask);
}

void BSP_LOG_Poll(void)
{
    uint32_t now = HAL_GetTick();
    uint32_t primask = __get_PRIMASK();
    HAL_StatusTypeDef result;
    uint16_t length;

    __disable_irq();
    if (tx_error || (tx_length != 0U && (uint32_t)(now - tx_tick) >= DEBUG_TX_TIMEOUT_MS))
    {
        /* USART1 uses IT transmission, so abort only disables IRQs and resets the handle. */
        HAL_UART_AbortTransmit(&huart1);
        tail = (uint16_t)((tail + tx_length) % DEBUG_TX_BUFFER_SIZE);
        data.dropped_bytes += tx_length;
        ++data.errors;
        tx_length = 0U;
        tx_error = 0U;
    }
    if (tx_length == 0U && head != tail)
    {
        length = head > tail ? (uint16_t)(head - tail) : (uint16_t)(DEBUG_TX_BUFFER_SIZE - tail);
        if (length > DEBUG_TX_CHUNK_SIZE)
            length = DEBUG_TX_CHUNK_SIZE;
        tx_length = length;
        tx_tick = now;
        result = HAL_UART_Transmit_IT(&huart1, &buffer[tail], length);
        if (result != HAL_OK)
        {
            tx_length = 0U;
            if (result != HAL_BUSY)
                tx_error = 1U;
        }
    }
    __set_PRIMASK(primask);
}

void BSP_LOG_TxComplete(void)
{
    tail = (uint16_t)((tail + tx_length) % DEBUG_TX_BUFFER_SIZE);
    data.sent_bytes += tx_length;
    tx_length = 0U;
}

void BSP_LOG_TxError(void)
{
    tx_error = 1U;
}

const bsp_log_data_t *BSP_LOG_GetData(void)
{
    return &data;
}
