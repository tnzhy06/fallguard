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
#include "dma.h"
#include "i2c.h"
#include "i2s.h"
#include "rtc.h"
#include "spi.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "app_main.h"
#include "app_ui.h"
#include "bsp_esp.h"
#include "bsp_gps.h"
#include "bsp_log.h"
#include "bsp_mpu6050.h"
#include "bsp_rtc.h"
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
static uint8_t esp_rx_byte;
static volatile uint8_t esp_rx_retry;
static uint32_t esp_rx_retry_tick;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/* Keep the generated main clock tree, but reserve the optional LSE/RTC for the
 * recoverable RTC initializer. These adapters survive CubeMX regeneration. */
static HAL_StatusTypeDef boot_osc_config(RCC_OscInitTypeDef *oscillator) {
  oscillator->OscillatorType &= ~RCC_OSCILLATORTYPE_LSE;
  return HAL_RCC_OscConfig(oscillator);
}

static HAL_StatusTypeDef boot_periph_clock_config(RCC_PeriphCLKInitTypeDef *clock) {
  clock->PeriphClockSelection &= ~RCC_PERIPHCLK_RTC;
  return clock->PeriphClockSelection ? HAL_RCCEx_PeriphCLKConfig(clock) : HAL_OK;
}

#define HAL_RCC_OscConfig boot_osc_config
#define HAL_RCCEx_PeriphCLKConfig boot_periph_clock_config

/* Only used before application startup. IRQs remain enabled and log TX is serviced.
 * Bound the wait so a failed display never prevents the application from booting. */
static void boot_log_step(const char *message, app_ui_boot_level_t level) {
  uint32_t start = HAL_GetTick();
  app_ui_boot_log(message, level);
  do {
    app_ui_poll();
    BSP_LOG_Poll();
  } while ((uint32_t)(HAL_GetTick() - start) < APP_BOOT_DISPLAY_TIMEOUT_MS &&
           ((uint32_t)(HAL_GetTick() - start) < APP_BOOT_LOG_STEP_MS ||
            app_ui_frame_pending()));
}

static void gps_receive_start(void) {
  HAL_StatusTypeDef status = HAL_UART_Receive_IT(&huart2, &gps_rx_byte, 1U);

  gps_rx_retry =
      (status != HAL_OK) && (huart2.RxState != HAL_UART_STATE_BUSY_RX);
  if (gps_rx_retry != 0U)
    BSP_GPS_RxError(HAL_UART_GetError(&huart2) | 0x80000000UL);
}

static void esp_receive_start(void) {
  HAL_StatusTypeDef status = HAL_UART_Receive_IT(&huart3, &esp_rx_byte, 1U);

  esp_rx_retry =
      (status != HAL_OK) && (huart3.RxState != HAL_UART_STATE_BUSY_RX);
  if (esp_rx_retry != 0U)
    BSP_ESP_RxError(HAL_UART_GetError(&huart3) | 0x80000000UL);
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
  /* Order startup below so the boot log is visible before optional RTC startup.
   * Skip CubeMX's automatic call list without changing its generated functions. */
  goto boot_peripherals;
  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_I2C1_Init();
  MX_RTC_Init();
  MX_TIM2_Init();
  MX_USART1_UART_Init();
  MX_USART2_UART_Init();
  MX_USART3_UART_Init();
  MX_I2S2_Init();
  MX_SPI1_Init();
  /* USER CODE BEGIN 2 */
boot_peripherals:
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_SPI1_Init();
  MX_USART1_UART_Init();
  BSP_LOG_Init();
  setvbuf(stdout, NULL, _IONBF, 0);
  app_ui_init();
  boot_log_step("主时钟已就绪", APP_UI_BOOT_OK);
  boot_log_step("显示接口已就绪", APP_UI_BOOT_OK);
  boot_log_step("RTC 时钟启动中", APP_UI_BOOT_WAIT);
  MX_RTC_Init();
  boot_log_step(BSP_RTC_IsHardwareReady() ? "RTC 时钟已就绪" : "RTC 故障 继续启动",
                BSP_RTC_IsHardwareReady() ? APP_UI_BOOT_OK : APP_UI_BOOT_WARN);
  MX_I2C1_Init();
  boot_log_step("姿态总线已就绪", APP_UI_BOOT_OK);
  MX_TIM2_Init();
  boot_log_step("计时接口已就绪", APP_UI_BOOT_OK);
  MX_USART2_UART_Init();
  boot_log_step("定位串口已就绪", APP_UI_BOOT_OK);
  MX_USART3_UART_Init();
  boot_log_step("网络串口已就绪", APP_UI_BOOT_OK);
  MX_I2S2_Init();
  boot_log_step("音频接口已就绪", APP_UI_BOOT_OK);

  LOG_INFO(DEBUG_LOG_BOOT_ENABLE, LOG_TAG_BOOT, "fallguard\r\n");
  LOG_INFO(DEBUG_LOG_BOOT_ENABLE, LOG_TAG_BOOT,
           "HCLK=%lu Hz LSE_READY=%lu\r\n",
           (unsigned long)HAL_RCC_GetHCLKFreq(),
           (unsigned long)(__HAL_RCC_GET_FLAG(RCC_FLAG_LSERDY) != RESET));
  if (!BSP_RTC_IsHardwareReady())
    LOG_WARN(DEBUG_LOG_RTC_ENABLE, LOG_TAG_RTC,
             "LSE/RTC unavailable; time invalid, local functions continue\r\n");

  if (HAL_TIM_Base_Start(&htim2) != HAL_OK) {
    LOG_ERROR(DEBUG_LOG_TIM_ENABLE, LOG_TAG_TIM, "TIM2 start failed\r\n");
    Error_Handler();
  }

  LOG_INFO(DEBUG_LOG_BOOT_ENABLE, LOG_TAG_BOOT,
           "MPU PB6=SCL PB7=SDA PA1=INT @0x%02X; calibration=%u samples, "
           "keep STILL\r\n",
           (unsigned int)MPU6050_I2C_ADDRESS,
           (unsigned int)BSP_MPU6050_CALIBRATION_SAMPLES);
  app_init();
  gps_receive_start();
  gps_rx_retry_tick = HAL_GetTick();
  esp_receive_start();
  esp_rx_retry_tick = HAL_GetTick();
  app_ui_boot_hardware_done();
  LOG_INFO(DEBUG_LOG_BOOT_ENABLE, LOG_TAG_BOOT,
           "GPS TX->PA3, USART2 115200 8N1, stale=%lu ms\r\n",
           (unsigned long)GPS_STALE_MS);

  LOG_INFO(DEBUG_LOG_BOOT_ENABLE, LOG_TAG_BOOT,
           "TIM2 started; MQTT/RTC pages show network and time\r\n");
  LOG_INFO(DEBUG_LOG_BOOT_ENABLE, LOG_TAG_BOOT,
           "LCD ST7789 240x240 SPI1 9MHz PA5=SCK PA7=MOSI; init pending\r\n");
  LOG_INFO(DEBUG_LOG_BOOT_ENABLE, LOG_TAG_BOOT,
           "KEY K1=LCD page; K2=test alarm; debounce=%lu ms\r\n",
           (unsigned long)KEY_DEBOUNCE_MS);
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1) {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    if ((gps_rx_retry != 0U) &&
        ((uint32_t)(HAL_GetTick() - gps_rx_retry_tick) >=
         APP_UART_RX_RETRY_MS)) {
      gps_rx_retry_tick = HAL_GetTick();
      gps_receive_start();
    }
    if ((esp_rx_retry != 0U) &&
        ((uint32_t)(HAL_GetTick() - esp_rx_retry_tick) >=
         APP_UART_RX_RETRY_MS)) {
      esp_rx_retry_tick = HAL_GetTick();
      esp_receive_start();
    }
    app_run();
    BSP_LOG_Poll();
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
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_RTC|RCC_PERIPHCLK_I2S2;
  PeriphClkInit.RTCClockSelection = RCC_RTCCLKSOURCE_LSE;
  PeriphClkInit.I2s2ClockSelection = RCC_I2S2CLKSOURCE_SYSCLK;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
  if (GPIO_Pin == MPU6050_INT_Pin)
    BSP_MPU6050_DataReadyIRQ();
}

void HAL_I2C_MemRxCpltCallback(I2C_HandleTypeDef *hi2c) {
  if (hi2c == &hi2c1)
    BSP_MPU6050_ReadCompleteIRQ();
}

void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *hi2c) {
  if (hi2c == &hi2c1)
    BSP_MPU6050_ReadErrorIRQ(HAL_I2C_GetError(hi2c));
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
  if (huart == &huart2) {
    /* Rearming clears ErrorCode before HAL can finish handling an overrun. */
    if (HAL_UART_GetError(huart) == HAL_UART_ERROR_NONE) {
      BSP_GPS_RxByte(gps_rx_byte);
      gps_receive_start();
    }
  } else if (huart == &huart3) {
    if (HAL_UART_GetError(huart) == HAL_UART_ERROR_NONE) {
      BSP_ESP_RxByte(esp_rx_byte);
      esp_receive_start();
    }
  }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart) {
  if (huart == &huart1) {
    BSP_LOG_TxError();
  } else if (huart == &huart2) {
    BSP_GPS_RxError(HAL_UART_GetError(huart));
    if (huart->RxState == HAL_UART_STATE_READY)
      gps_receive_start();
  } else if (huart == &huart3) {
    BSP_ESP_RxError(HAL_UART_GetError(huart));
    if (huart->RxState == HAL_UART_STATE_READY)
      esp_receive_start();
  }
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart) {
  if (huart == &huart1)
    BSP_LOG_TxComplete();
}

int _write(int file, char *ptr, int len) {
  (void)file;

  if (len == 0) {
    return 0;
  }
  if ((ptr == NULL) || (len < 0) || (len > UINT16_MAX)) {
    errno = EINVAL;
    return -1;
  }

  /* A full log buffer drops excess output without blocking or poisoning stdout.
   */
  BSP_LOG_Write(ptr, (uint16_t)len);
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
  while (1) {
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
  /* User can add his own implementation to report the file name and line
     number, ex: printf("Wrong parameters value: file %s on line %d\r\n", file,
     line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
