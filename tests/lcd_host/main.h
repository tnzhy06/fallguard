#ifndef HOST_MAIN_H
#define HOST_MAIN_H
#include <stdint.h>
typedef enum { GPIO_PIN_RESET, GPIO_PIN_SET } GPIO_PinState;
typedef struct { unsigned unused; } GPIO_TypeDef;
typedef enum { HAL_OK, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT } HAL_StatusTypeDef;
typedef enum { HAL_SPI_STATE_READY, HAL_SPI_STATE_BUSY_TX } HAL_SPI_StateTypeDef;
typedef struct { HAL_SPI_StateTypeDef state; uint32_t error; } SPI_HandleTypeDef;
#define HAL_SPI_ERROR_NONE 0U
#define LCD_CS_GPIO_Port ((GPIO_TypeDef *)1)
#define LCD_DC_GPIO_Port ((GPIO_TypeDef *)1)
#define LCD_RST_GPIO_Port ((GPIO_TypeDef *)1)
#define LCD_BL_GPIO_Port ((GPIO_TypeDef *)1)
#define LCD_CS_Pin 1U
#define LCD_DC_Pin 2U
#define LCD_RST_Pin 4U
#define LCD_BL_Pin 8U
uint32_t HAL_GetTick(void);
void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState level);
HAL_StatusTypeDef HAL_SPI_Transmit(SPI_HandleTypeDef *, uint8_t *, uint16_t, uint32_t);
HAL_StatusTypeDef HAL_SPI_Transmit_DMA(SPI_HandleTypeDef *, uint8_t *, uint16_t);
HAL_SPI_StateTypeDef HAL_SPI_GetState(SPI_HandleTypeDef *);
uint32_t HAL_SPI_GetError(SPI_HandleTypeDef *);
HAL_StatusTypeDef HAL_SPI_Abort(SPI_HandleTypeDef *);
#endif
