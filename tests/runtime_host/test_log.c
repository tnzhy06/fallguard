#include "bsp_log.h"
#include "usart.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

UART_HandleTypeDef huart1;
static uint32_t ticks, irq_mask;
static unsigned starts, aborts;
static HAL_StatusTypeDef start_result;
static const uint8_t *pending;
static uint16_t pending_length;
static uint8_t snapshot[DEBUG_TX_CHUNK_SIZE];
static uint8_t received[8192];
static size_t received_length;

uint32_t HAL_GetTick(void) { return ticks; }
uint32_t __get_PRIMASK(void) { return irq_mask; }
void __disable_irq(void) { irq_mask = 1U; }
void __set_PRIMASK(uint32_t mask) { irq_mask = mask; }

HAL_StatusTypeDef HAL_UART_Transmit_IT(UART_HandleTypeDef *uart, const uint8_t *bytes, uint16_t size)
{
    assert(uart == &huart1 && irq_mask == 1U);
    assert(size != 0U && size <= DEBUG_TX_CHUNK_SIZE && pending_length == 0U);
    ++starts;
    if (start_result != HAL_OK)
        return start_result;
    pending = bytes;
    pending_length = size;
    memcpy(snapshot, bytes, size);
    huart1.gState = HAL_UART_STATE_BUSY_TX;
    return HAL_OK;
}

HAL_StatusTypeDef HAL_UART_AbortTransmit(UART_HandleTypeDef *uart)
{
    assert(uart == &huart1 && irq_mask == 1U);
    ++aborts;
    pending_length = 0U;
    huart1.gState = HAL_UART_STATE_READY;
    return HAL_OK;
}

static void complete_tx(void)
{
    assert(pending_length != 0U);
    assert(memcmp(pending, snapshot, pending_length) == 0);
    assert(received_length + pending_length <= sizeof(received));
    memcpy(received + received_length, pending, pending_length);
    received_length += pending_length;
    pending_length = 0U;
    huart1.gState = HAL_UART_STATE_READY;
    BSP_LOG_TxComplete();
}

static void drain(void)
{
    for (unsigned i = 0U; i < 128U; ++i)
    {
        BSP_LOG_Poll();
        if (pending_length == 0U)
            return;
        complete_tx();
    }
    assert(0 && "The log queue did not drain");
}

static void reset(void)
{
    ticks = irq_mask = starts = aborts = 0U;
    pending_length = 0U;
    received_length = 0U;
    start_result = HAL_OK;
    huart1.gState = HAL_UART_STATE_READY;
    BSP_LOG_Init();
}

static void test_fifo_wrap_and_inflight_ownership(void)
{
    char text[3000];
    for (unsigned i = 0U; i < sizeof(text); ++i)
        text[i] = (char)(i % 93U + 33U);
    reset();
    BSP_LOG_Write(text, 1500U);
    assert(starts == 0U); /* Enqueue never calls a UART wait or transmission. */
    BSP_LOG_Poll();
    BSP_LOG_Write(text + 1500U, 100U); /* Must preserve the in-flight pointer's bytes. */
    for (unsigned i = 0U; i < 16U; ++i)
    {
        complete_tx();
        BSP_LOG_Poll();
    }
    BSP_LOG_Write(text + 1600U, 1400U); /* Cross the ring's end while TX is active. */
    drain();
    assert(received_length == sizeof(text));
    assert(memcmp(received, text, sizeof(text)) == 0);
    assert(BSP_LOG_GetData()->sent_bytes == sizeof(text));
    assert(BSP_LOG_GetData()->dropped_bytes == 0U);
    BSP_LOG_TxComplete(); /* A late callback must not advance the queue. */
    assert(BSP_LOG_GetData()->sent_bytes == sizeof(text));
}

static void test_overflow_and_interrupt_mask(void)
{
    char text[4096];
    memset(text, 'L', sizeof(text));
    reset();
    irq_mask = 1U;
    BSP_LOG_Write(text, sizeof(text));
    assert(irq_mask == 1U);
    assert(BSP_LOG_GetData()->dropped_bytes == sizeof(text) - (DEBUG_TX_BUFFER_SIZE - 1U));
    BSP_LOG_Poll();
    assert(irq_mask == 1U);
    BSP_LOG_Write("x", 1U);
    assert(BSP_LOG_GetData()->dropped_bytes == sizeof(text) - DEBUG_TX_BUFFER_SIZE + 2U);
    irq_mask = 0U;
    drain();
    assert(irq_mask == 0U && received_length == DEBUG_TX_BUFFER_SIZE - 1U);
    assert(memcmp(received, text, received_length) == 0);
}

static void test_errors_busy_and_timeout_wrap(void)
{
    char text[100];
    for (unsigned i = 0U; i < sizeof(text); ++i) text[i] = (char)i;
    reset();
    BSP_LOG_Write(text, sizeof(text));
    start_result = HAL_BUSY;
    BSP_LOG_Poll();
    assert(pending_length == 0U && BSP_LOG_GetData()->dropped_bytes == 0U);
    start_result = HAL_OK;
    drain();
    assert(received_length == sizeof(text) && memcmp(received, text, sizeof(text)) == 0);

    reset();
    BSP_LOG_Write(text, sizeof(text));
    start_result = HAL_ERROR;
    BSP_LOG_Poll();
    start_result = HAL_OK;
    drain();
    assert(aborts == 1U && BSP_LOG_GetData()->errors == 1U);
    assert(received_length == sizeof(text));

    reset();
    BSP_LOG_Write(text, sizeof(text));
    BSP_LOG_Poll();
    BSP_LOG_TxError();
    drain();
    assert(aborts == 1U && BSP_LOG_GetData()->dropped_bytes == DEBUG_TX_CHUNK_SIZE);
    assert(received_length == sizeof(text) - DEBUG_TX_CHUNK_SIZE);
    assert(memcmp(received, text + DEBUG_TX_CHUNK_SIZE, received_length) == 0);

    reset();
    ticks = UINT32_MAX - 50U;
    BSP_LOG_Write(text, sizeof(text));
    BSP_LOG_Poll();
    ticks += DEBUG_TX_TIMEOUT_MS - 1U;
    BSP_LOG_Poll();
    assert(aborts == 0U);
    ++ticks;
    BSP_LOG_Poll();
    assert(aborts == 1U && BSP_LOG_GetData()->errors == 1U);
    drain();
    assert(memcmp(received, text + DEBUG_TX_CHUNK_SIZE, received_length) == 0);
    BSP_LOG_Write("recovered", 9U);
    drain();
    assert(memcmp(received + received_length - 9U, "recovered", 9U) == 0);
}

int main(void)
{
    test_fifo_wrap_and_inflight_ownership();
    test_overflow_and_interrupt_mask();
    test_errors_busy_and_timeout_wrap();
    puts("Nonblocking UART log tests passed");
    return 0;
}
