#ifndef BSP_LED_H
#define BSP_LED_H

typedef enum
{
    BSP_LED_OFF,
    BSP_LED_NORMAL,
    BSP_LED_ALARM,
    BSP_LED_OFFLINE
} bsp_led_mode_t;

void BSP_LED_Init(void);
void BSP_LED_SetMode(bsp_led_mode_t mode);
void BSP_LED_Poll(void);

#endif
