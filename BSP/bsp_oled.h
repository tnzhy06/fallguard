#ifndef BSP_OLED_H
#define BSP_OLED_H

#include <stdint.h>

#define BSP_OLED_8X16 8U
#define BSP_OLED_6X8 6U
#define BSP_OLED_UNFILLED 0U
#define BSP_OLED_FILLED 1U

typedef enum
{
    BSP_OLED_WAIT_POWER,
    BSP_OLED_CONFIGURING,
    BSP_OLED_READY,
    BSP_OLED_ERROR
} bsp_oled_state_t;

void BSP_OLED_Init(void);
void BSP_OLED_Poll(void);
bsp_oled_state_t BSP_OLED_GetState(void);
uint8_t BSP_OLED_IsBusy(void);
void BSP_OLED_Update(void);
void BSP_OLED_Clear(void);
void BSP_OLED_ClearArea(uint8_t X, uint8_t Y, uint8_t Width, uint8_t Height);
void BSP_OLED_Reverse(void);
void BSP_OLED_ShowChar(uint8_t X, uint8_t Y, char Char, uint8_t FontSize);
void BSP_OLED_ShowString(uint8_t X, uint8_t Y, const char *String, uint8_t FontSize);
void BSP_OLED_ShowChinese(uint8_t X, uint8_t Y, const char *Chinese);
void BSP_OLED_ShowImage(uint8_t X, uint8_t Y, uint8_t Width, uint8_t Height,
                        const uint8_t *Image);
void BSP_OLED_DrawPoint(int16_t X, int16_t Y);
void BSP_OLED_DrawLine(uint8_t X0, uint8_t Y0, uint8_t X1, uint8_t Y1);
void BSP_OLED_DrawRectangle(uint8_t X, uint8_t Y, uint8_t Width,
                            uint8_t Height, uint8_t IsFilled);
void BSP_OLED_DrawCircle(uint8_t X, uint8_t Y, uint8_t Radius, uint8_t IsFilled);

#endif
