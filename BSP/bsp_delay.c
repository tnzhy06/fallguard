#include "bsp_delay.h"
#include "tim.h"

void BSP_DELAY_Us(uint16_t us)
{
    uint16_t start = (uint16_t)__HAL_TIM_GET_COUNTER(&htim2);

    /* TIM2 is a 16-bit free-running counter, including across its rollover. */
    while ((uint16_t)((uint16_t)__HAL_TIM_GET_COUNTER(&htim2) - start) < us)
    {
    }
}
