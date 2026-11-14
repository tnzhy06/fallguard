/*
 * Adapted from Floyd-Fish/ST7789-STM32 (GPL-3.0), commit
 * 44ffcfefab97e1d225cbb9b1b4200cfd37093192.
 * Retains the upstream register initialization, rotation and address-window
 * protocol. Fallguard uses cooperative reset/sleep delays, bounded command
 * writes and asynchronous strips instead of the upstream DMA busy-wait.
 */
#include "st7789.h"
#include "spi.h"

static st7789_state_t state;
static uint32_t state_tick, transfer_tick;
static uint8_t init_index, transferring, display_on;

typedef struct { uint8_t cmd, length, data[14]; } init_command_t;
static const init_command_t init_commands[] = {
    {0x3A, 1, {0x55}},
    {0xB2, 5, {0x0C, 0x0C, 0x00, 0x33, 0x33}},
#if ST7789_ROTATION == 0
    {0x36, 1, {0xC0 | (ST7789_BGR ? 8 : 0)}},
#elif ST7789_ROTATION == 1
    {0x36, 1, {0xA0 | (ST7789_BGR ? 8 : 0)}},
#elif ST7789_ROTATION == 2
    {0x36, 1, {0x00 | (ST7789_BGR ? 8 : 0)}},
#else
    {0x36, 1, {0x60 | (ST7789_BGR ? 8 : 0)}},
#endif
    {0xB7, 1, {0x35}}, {0xBB, 1, {0x19}}, {0xC0, 1, {0x2C}},
    {0xC2, 1, {0x01}}, {0xC3, 1, {0x12}}, {0xC4, 1, {0x20}},
    {0xC6, 1, {0x0F}}, {0xD0, 2, {0xA4, 0xA1}},
    {0xE0, 14, {0xD0, 0x04, 0x0D, 0x11, 0x13, 0x2B, 0x3F, 0x54, 0x4C, 0x18, 0x0D, 0x0B, 0x1F, 0x23}},
    {0xE1, 14, {0xD0, 0x04, 0x0C, 0x11, 0x13, 0x2C, 0x3F, 0x44, 0x51, 0x2F, 0x1F, 0x1F, 0x20, 0x23}},
    {ST7789_INVERSION ? 0x21 : 0x20, 0, {0}},
    {0x11, 0, {0}}
};

static void select_panel(GPIO_PinState level)
{
#if ST7789_USE_CS
    HAL_GPIO_WritePin(LCD_CS_GPIO_Port, LCD_CS_Pin, level);
#else
    (void)level;
#endif
}

static void fail(void)
{
    select_panel(GPIO_PIN_SET);
    HAL_GPIO_WritePin(LCD_BL_GPIO_Port, LCD_BL_Pin,
                      ST7789_BL_ACTIVE == GPIO_PIN_SET ? GPIO_PIN_RESET : GPIO_PIN_SET);
    transferring = display_on = 0U;
    state = ST7789_ERROR;
    state_tick = HAL_GetTick();
}

/* CS remains asserted until both command and its parameters are transmitted. */
static uint8_t command(uint8_t cmd, const uint8_t *data, uint16_t length)
{
    select_panel(GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LCD_DC_GPIO_Port, LCD_DC_Pin, GPIO_PIN_RESET);
    if (HAL_SPI_Transmit(&hspi1, &cmd, 1U, LCD_COMMAND_TIMEOUT_MS) != HAL_OK)
        goto error;
    if (length != 0U)
    {
        HAL_GPIO_WritePin(LCD_DC_GPIO_Port, LCD_DC_Pin, GPIO_PIN_SET);
        if (HAL_SPI_Transmit(&hspi1, (uint8_t *)data, length, LCD_COMMAND_TIMEOUT_MS) != HAL_OK)
            goto error;
    }
    select_panel(GPIO_PIN_SET);
    return 1U;
error:
    fail();
    return 0U;
}

void ST7789_Init(void)
{
    select_panel(GPIO_PIN_SET);
    HAL_GPIO_WritePin(LCD_RST_GPIO_Port, LCD_RST_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(LCD_BL_GPIO_Port, LCD_BL_Pin,
                      ST7789_BL_ACTIVE == GPIO_PIN_SET ? GPIO_PIN_RESET : GPIO_PIN_SET);
    transferring = display_on = init_index = 0U;
    state = ST7789_WAIT_POWER;
    state_tick = HAL_GetTick();
}

void ST7789_Poll(void)
{
    uint32_t now = HAL_GetTick(), age = (uint32_t)(now - state_tick);
    if (transferring)
    {
        if (HAL_SPI_GetState(&hspi1) == HAL_SPI_STATE_READY)
        {
            transferring = 0U;
            select_panel(GPIO_PIN_SET);
            if (HAL_SPI_GetError(&hspi1) != HAL_SPI_ERROR_NONE)
                fail();
        }
        else if ((uint32_t)(now - transfer_tick) >= LCD_TRANSFER_TIMEOUT_MS)
        {
            HAL_SPI_Abort(&hspi1);
            fail();
        }
        return;
    }
    switch (state)
    {
    case ST7789_WAIT_POWER:
        if (age < LCD_POWER_DELAY_MS) break;
        HAL_GPIO_WritePin(LCD_RST_GPIO_Port, LCD_RST_Pin, GPIO_PIN_RESET);
        state = ST7789_RESET_LOW; state_tick = now;
        break;
    case ST7789_RESET_LOW:
        if (age < LCD_RESET_LOW_MS) break;
        HAL_GPIO_WritePin(LCD_RST_GPIO_Port, LCD_RST_Pin, GPIO_PIN_SET);
        state = ST7789_RESET_HIGH; state_tick = now;
        break;
    case ST7789_RESET_HIGH:
        if (age >= LCD_RESET_HIGH_MS) state = ST7789_CONFIGURING;
        break;
    case ST7789_CONFIGURING:
    {
        const init_command_t *c = &init_commands[init_index];
        if (!command(c->cmd, c->data, c->length)) break;
        if (++init_index == sizeof(init_commands) / sizeof(init_commands[0]))
        {
            state = ST7789_WAIT_SLEEP_OUT; state_tick = now;
        }
        break;
    }
    case ST7789_WAIT_SLEEP_OUT:
        if ((age >= LCD_SLEEP_OUT_MS) && command(0x13, 0, 0U)) state = ST7789_READY;
        break;
    case ST7789_ERROR:
        if (age >= LCD_RETRY_MS) ST7789_Init();
        break;
    default: break;
    }
}

st7789_state_t ST7789_GetState(void) { return state; }
uint8_t ST7789_IsBusy(void) { return transferring; }

uint8_t ST7789_WriteRows(uint16_t y, uint16_t rows, const uint8_t *pixels)
{
    uint16_t xs = ST7789_X_SHIFT, xe = xs + ST7789_WIDTH - 1U;
    uint16_t ys = y + ST7789_Y_SHIFT, ye = ys + rows - 1U;
    uint8_t xdata[] = {xs >> 8, xs & 255U, xe >> 8, xe & 255U};
    uint8_t ydata[] = {ys >> 8, ys & 255U, ye >> 8, ye & 255U};
    if ((state != ST7789_READY) || transferring || !pixels || !rows ||
        (rows > LCD_BAND_ROWS) || (y >= ST7789_HEIGHT) || (rows > ST7789_HEIGHT - y))
        return 0U;
    if (!command(0x2A, xdata, sizeof(xdata)) ||
        !command(0x2B, ydata, sizeof(ydata)) || !command(0x2C, 0, 0U))
        return 0U;
    select_panel(GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LCD_DC_GPIO_Port, LCD_DC_Pin, GPIO_PIN_SET);
    if (HAL_SPI_Transmit_DMA(&hspi1, (uint8_t *)pixels, rows * ST7789_WIDTH * 2U) != HAL_OK)
    {
        fail();
        return 0U;
    }
    transferring = 1U;
    transfer_tick = HAL_GetTick();
    return 1U;
}

void ST7789_DisplayOn(void)
{
    if ((state == ST7789_READY) && !transferring && !display_on && command(0x29, 0, 0U))
    {
        display_on = 1U;
        HAL_GPIO_WritePin(LCD_BL_GPIO_Port, LCD_BL_Pin, ST7789_BL_ACTIVE);
    }
}
