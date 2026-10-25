/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "i2c.h"
#include "rtc.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "app_main.h"
#include "debug_config.h"
#include "bsp_gps.h"
#include <errno.h>
#include <stdio.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
static uint8_t gps_rx_byte;
static volatile uint8_t gps_rx_retry;
static uint32_t gps_rx_retry_tick;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
static void gps_receive_start(void)
{
    HAL_StatusTypeDef status = HAL_UART_Receive_IT(&huart2, &gps_rx_byte, 1U);

    gps_rx_retry = (status != HAL_OK) && (huart2.RxState != HAL_UART_STATE_BUSY_RX);
    if (gps_rx_retry != 0U)
        BSP_GPS_RxError(HAL_UART_GetError(&huart2) | 0x80000000UL);
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_I2C1_Init();
  MX_RTC_Init();
  MX_TIM2_Init();
  MX_USART1_UART_Init();
  MX_USART2_UART_Init();
  MX_USART3_UART_Init();
  /* USER CODE BEGIN 2 */
  setvbuf(stdout, NULL, _IONBF, 0);

  DEBUG_PRINT(DEBUG_LOG_BOOT, "\r\n=== fallguard GPS + MPU6050 ===\r\n");
  DEBUG_PRINT(DEBUG_LOG_BOOT, "[C05] HCLK=%lu Hz LSE_READY=%lu\r\n",
         (unsigned long)HAL_RCC_GetHCLKFreq(),
         (unsigned long)(__HAL_RCC_GET_FLAG(RCC_FLAG_LSERDY) != RESET));

  if (HAL_TIM_Base_Start(&htim2) != HAL_OK)
  {
    DEBUG_PRINT(DEBUG_LOG_ERRORS, "[C05] TIM2 start failed\r\n");
    Error_Handler();
  }

  DEBUG_PRINT(DEBUG_LOG_BOOT, "[MPU] PB6=SCL PB7=SDA @0x68; keep STILL (~3s), 100Hz 2g/250dps\r\n");
  app_init();
  gps_receive_start();
  gps_rx_retry_tick = HAL_GetTick();
  DEBUG_PRINT(DEBUG_LOG_BOOT, "[C05] GPS: TX->PA3, USART2 115200 8N1, stale=5000ms\r\n");

  DEBUG_PRINT(DEBUG_LOG_BOOT, "[C05] TIM2 started; RTC time uncalibrated\r\n");
  DEBUG_PRINT(DEBUG_LOG_BOOT, "[C05] OLED: PB4=SCL PB5=SDA @0x3C; init pending\r\n");
  DEBUG_PRINT(DEBUG_LOG_BOOT, "[C05] K1=OLED page; K2=test alarm; debounce=20ms\r\n");
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    if ((gps_rx_retry != 0U) &&
        ((uint32_t)(HAL_GetTick() - gps_rx_retry_tick) >= 5U))
    {
      gps_rx_retry_tick = HAL_GetTick();
      gps_receive_start();
    }
    app_run();
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE|RCC_OSCILLATORTYPE_LSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.LSEState = RCC_LSE_ON;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_RTC;
  PeriphClkInit.RTCClockSelection = RCC_RTCCLKSOURCE_LSE;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart == &huart2)
    {
        /* Rearming clears ErrorCode before HAL can finish handling an overrun. */
        if (HAL_UART_GetError(huart) == HAL_UART_ERROR_NONE)
        {
            BSP_GPS_RxByte(gps_rx_byte);
            gps_receive_start();
        }
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart == &huart2)
    {
        BSP_GPS_RxError(HAL_UART_GetError(huart));
        if (huart->RxState == HAL_UART_STATE_READY)
            gps_receive_start();
    }
}

int _write(int file, char *ptr, int len)
{
    (void)file;

    if (len == 0)
    {
        return 0;
    }
    if ((len < 0) || (len > UINT16_MAX))
    {
        errno = EINVAL;
        return -1;
    }

    /* Bound logging time so a UART fault cannot stall the polling loop. */
    if (HAL_UART_Transmit(&huart1, (uint8_t *)ptr, (uint16_t)len, 20U) != HAL_OK)
    {
        errno = EIO;
        return -1;
    }
    return len;
}
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
