#include "bsp_led.h"
#include "main.h"

static bsp_led_mode_t led_mode;
static uint32_t led_last_tick;

void BSP_LED_Init(void)
{
    led_mode = BSP_LED_OFF;
    led_last_tick = HAL_GetTick();
    HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET);
}

void BSP_LED_SetMode(bsp_led_mode_t mode)
{
    /* The caller may repeat the same mode every loop without restarting a blink. */
    if (mode == led_mode)
    {
        return;
    }

    led_mode = mode;
    led_last_tick = HAL_GetTick();
    HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin,
                      (mode == BSP_LED_OFF) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void BSP_LED_Poll(void)
{
    uint32_t interval;
    uint32_t now = HAL_GetTick();

    if (led_mode == BSP_LED_NORMAL)
    {
        interval = 500U;
    }
    else if (led_mode == BSP_LED_ALARM)
    {
        interval = 100U;
    }
    else
    {
        return;
    }

    if ((uint32_t)(now - led_last_tick) >= interval)
    {
        led_last_tick = now;
        HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
    }
}
