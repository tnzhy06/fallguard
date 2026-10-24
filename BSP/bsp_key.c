#include "bsp_key.h"
#include "main.h"

#define KEY_DEBOUNCE_MS 20U

typedef struct
{
    GPIO_PinState raw;
    GPIO_PinState stable;
    uint32_t changed_tick;
} key_state_t;

static key_state_t keys[2];

void BSP_KEY_Init(void)
{
    uint32_t now = HAL_GetTick();

    keys[0].raw = HAL_GPIO_ReadPin(KEY1_GPIO_Port, KEY1_Pin);
    keys[1].raw = HAL_GPIO_ReadPin(KEY2_GPIO_Port, KEY2_Pin);

    for (uint8_t i = 0; i < 2U; ++i)
    {
        /* A key held during boot must be released before it can trigger. */
        keys[i].stable = keys[i].raw;
        keys[i].changed_tick = now;
    }
}

uint8_t BSP_KEY_Scan(void)
{
    GPIO_PinState levels[2] = {
        HAL_GPIO_ReadPin(KEY1_GPIO_Port, KEY1_Pin),
        HAL_GPIO_ReadPin(KEY2_GPIO_Port, KEY2_Pin)
    };
    uint32_t now = HAL_GetTick();
    uint8_t pressed = 0U;

    for (uint8_t i = 0; i < 2U; ++i)
    {
        if (levels[i] != keys[i].raw)
        {
            keys[i].raw = levels[i];
            keys[i].changed_tick = now;
        }
        else if ((keys[i].stable != keys[i].raw) &&
                 ((uint32_t)(now - keys[i].changed_tick) >= KEY_DEBOUNCE_MS))
        {
            keys[i].stable = keys[i].raw;
            if (keys[i].stable == GPIO_PIN_RESET)
            {
                pressed |= (uint8_t)(1U << i);
            }
        }
    }

    return pressed;
}
