#include "app_ui.h"
#include "bsp_oled.h"
#include "stm32f1xx_hal.h"
#include <stdio.h>

static uint8_t page;
static uint8_t alarm_active;
static uint8_t dirty;
static uint32_t last_refresh;
static bsp_oled_state_t last_oled_state;

void app_ui_init(void)
{
    page = 0U;
    alarm_active = 0U;
    dirty = 1U;
    last_refresh = HAL_GetTick();
    last_oled_state = BSP_OLED_WAIT_POWER;
    BSP_OLED_Init();
}

void app_ui_next_page(void)
{
    page = (uint8_t)((page + 1U) % 3U);
    dirty = 1U;
}

void app_ui_set_alarm(uint8_t active)
{
    alarm_active = active;
    dirty = 1U;
}

static void draw_page(uint32_t now)
{
    char line[22];

    BSP_OLED_Clear();
    if (alarm_active != 0U)
    {
        BSP_OLED_ShowString(24U, 0U, "TEST ALARM", BSP_OLED_8X16);
        BSP_OLED_DrawLine(0U, 19U, 127U, 19U);
        BSP_OLED_ShowString(16U, 28U, "K2 TO CANCEL", BSP_OLED_8X16);
        BSP_OLED_ShowString(19U, 52U, "LOCAL TEST ONLY", BSP_OLED_6X8);
        BSP_OLED_Reverse();
        return;
    }

    if (page == 0U)
    {
        BSP_OLED_ShowString(0U, 0U, "FALLGUARD C04", BSP_OLED_8X16);
        BSP_OLED_ShowString(0U, 20U, "SSD1306 / SOFT I2C", BSP_OLED_6X8);
        snprintf(line, sizeof(line), "UP %lus", (unsigned long)(now / 1000U));
        BSP_OLED_ShowString(0U, 34U, line, BSP_OLED_6X8);
        BSP_OLED_ShowString(0U, 52U, "K1:PAGE  K2:ALARM", BSP_OLED_6X8);
    }
    else if (page == 1U)
    {
        BSP_OLED_ShowString(0U, 0U, "CHINESE", BSP_OLED_8X16);
        BSP_OLED_ShowChinese(16U, 22U, "你好，世界。");
        BSP_OLED_ShowString(0U, 52U, "UTF-8 / 16x16", BSP_OLED_6X8);
    }
    else
    {
        uint8_t width = (uint8_t)((now / 100U) % 109U);

        BSP_OLED_DrawRectangle(0U, 0U, 128U, 64U, BSP_OLED_UNFILLED);
        BSP_OLED_ShowString(4U, 3U, "GRAPHICS", BSP_OLED_6X8);
        BSP_OLED_DrawLine(4U, 15U, 123U, 15U);
        BSP_OLED_DrawRectangle(8U, 22U, 28U, 22U, BSP_OLED_UNFILLED);
        BSP_OLED_DrawCircle(60U, 34U, 11U, BSP_OLED_UNFILLED);
        BSP_OLED_DrawRectangle(88U, 23U, 24U, 20U, BSP_OLED_FILLED);
        BSP_OLED_DrawRectangle(8U, 52U, 112U, 8U, BSP_OLED_UNFILLED);
        BSP_OLED_DrawRectangle(10U, 54U, width, 4U, BSP_OLED_FILLED);
    }
}

void app_ui_poll(void)
{
    bsp_oled_state_t state;
    uint32_t now = HAL_GetTick();

    BSP_OLED_Poll();
    state = BSP_OLED_GetState();
    if (state != last_oled_state)
    {
        last_oled_state = state;
        if (state == BSP_OLED_READY)
        {
            printf("[C04] OLED init ACK OK @0x3C\r\n");
        }
        else if (state == BSP_OLED_ERROR)
        {
            printf("[C04] OLED bus/ACK failed; check PB4/PB5, pull-ups and 0x3C\r\n");
        }
    }

    /* One framebuffer is sufficient when drawing waits for the current transfer. */
    if ((state == BSP_OLED_READY) && (BSP_OLED_IsBusy() == 0U) &&
        ((dirty != 0U) || ((uint32_t)(now - last_refresh) >= 100U)))
    {
        draw_page(now);
        BSP_OLED_Update();
        dirty = 0U;
        last_refresh = now;
    }
}
