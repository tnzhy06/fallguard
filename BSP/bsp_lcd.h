#ifndef BSP_LCD_H
#define BSP_LCD_H

#include <stdint.h>

#define LCD_RGB(r,g,b) ((uint16_t)((((r) & 248U) << 8) | (((g) & 252U) << 3) | ((b) >> 3)))

/* All drawing is clipped to the current 240x8 strip. No heap allocation. */
void BSP_LCD_BeginBand(uint16_t y, uint16_t background);
const uint8_t *BSP_LCD_GetBand(void);
void BSP_LCD_Rect(int x, int y, int w, int h, uint16_t color);
void BSP_LCD_RoundRect(int x, int y, int w, int h, int radius, uint16_t color);
void BSP_LCD_Line(int x0, int y0, int x1, int y1, uint16_t color);
void BSP_LCD_Circle(int x, int y, int radius, uint16_t color);
/* Noto Sans SC: Chinese/ASCII 16/20px, with proportional ASCII advances.
 * A separate 40px subset is used for the alarm title. */
void BSP_LCD_Text(int x, int y, const char *text, uint8_t size, uint8_t scale,
                  uint16_t color, uint16_t background);
int BSP_LCD_TextWidth(const char *text, uint8_t size, uint8_t scale);
/* Native 32x52 clock cells, antialiased at the display resolution. */
void BSP_LCD_Clock(int x, int y, const char *text, uint16_t color, uint16_t background);
/* Antialiased square 2bpp icon; zero pixels preserve the existing background. */
void BSP_LCD_Icon(int x, int y, const uint8_t *mask, uint8_t size, uint16_t color, uint16_t background);

#endif
