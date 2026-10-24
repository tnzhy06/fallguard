#include "app_main.h"
#include "app_ui.h"
#include "bsp_key.h"
#include "bsp_led.h"
#include "stm32f1xx_hal.h"
#include <stdio.h>

static uint32_t key_last_tick;
static uint8_t test_alarm;

void app_init(void)
{
    BSP_LED_Init();
    BSP_KEY_Init();
    app_ui_init();
    key_last_tick = HAL_GetTick();
    test_alarm = 0U;
}

void app_run(void)
{
    uint32_t now = HAL_GetTick();

    if ((uint32_t)(now - key_last_tick) >= 5U)
    {
        uint8_t pressed;

        key_last_tick = now;
        pressed = BSP_KEY_Scan();
        if ((pressed & BSP_KEY1_PRESSED) != 0U)
        {
            app_ui_next_page();
            printf("[C03] K1: page next\r\n");
        }
        if ((pressed & BSP_KEY2_PRESSED) != 0U)
        {
            test_alarm ^= 1U;
            app_ui_set_alarm(test_alarm);
            printf("[C03] K2: test alarm %s\r\n",
                   test_alarm ? "ON" : "OFF");
        }
    }

    BSP_LED_SetMode(test_alarm ? BSP_LED_ALARM : BSP_LED_OFFLINE);
    BSP_LED_Poll();
    app_ui_poll();
}
