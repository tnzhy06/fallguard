/* Adapted from Floyd-Fish/ST7789-STM32, GPL-3.0. See README.md. */
#ifndef ST7789_H
#define ST7789_H

#include "main.h"
#include "app_config.h"

/* Address-window offsets derived from the configured panel rotation. */
#if ST7789_ROTATION == 0
#define ST7789_X_SHIFT 0U
#define ST7789_Y_SHIFT 80U
#elif ST7789_ROTATION == 1
#define ST7789_X_SHIFT 80U
#define ST7789_Y_SHIFT 0U
#elif ST7789_ROTATION == 2 || ST7789_ROTATION == 3
#define ST7789_X_SHIFT 0U
#define ST7789_Y_SHIFT 0U
#else
#error "ST7789_ROTATION must be 0..3"
#endif

typedef enum {
    ST7789_WAIT_POWER, ST7789_RESET_LOW, ST7789_RESET_HIGH,
    ST7789_CONFIGURING, ST7789_WAIT_SLEEP_OUT, ST7789_READY, ST7789_ERROR
} st7789_state_t;

void ST7789_Init(void);
void ST7789_Poll(void);
st7789_state_t ST7789_GetState(void);
uint8_t ST7789_IsBusy(void);
/* Data must be RGB565, high byte first, and remain unchanged until !IsBusy(). */
uint8_t ST7789_WriteRows(uint16_t y, uint16_t rows, const uint8_t *pixels);
void ST7789_DisplayOn(void);

#endif
