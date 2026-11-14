#include "bsp_mpu6050.h"
#include "mpu6050.h"
#include "i2c.h"
#include "main.h"
#include <string.h>

static bsp_mpu6050_state_t state;
static bsp_mpu6050_sample_t sample;
static mpu6050_data_t data;
static uint32_t phase_tick, last_data_tick, handled_irq;
static volatile uint32_t data_ready_irq, data_ready_tick;
static volatile uint8_t irq_armed;
static uint8_t start_phase;
static uint16_t calibration_samples;
static int32_t gyro_sum[3];
static double gyro_bias[3];

typedef enum
{
    RECOVERY_WAIT,
    RECOVERY_CLOCK_HIGH,
    RECOVERY_CLOCK_LOW,
    RECOVERY_STOP_LOW,
    RECOVERY_STOP_HIGH,
    RECOVERY_RELEASE
} recovery_step_t;

static recovery_step_t recovery_step;
static uint8_t recovery_pulses;

static void failed(void)
{
    irq_armed = 0U;
    state = BSP_MPU6050_ERROR;
    sample.samples = 0U;
    recovery_step = RECOVERY_WAIT;
    phase_tick = HAL_GetTick();
}

static void finish_recovery(uint8_t bus_free)
{
    /* HAL MSP restores PB6/PB7 to AF open-drain after the GPIO bus-clear sequence. */
    if ((HAL_I2C_Init(&hi2c1) == HAL_OK) && bus_free)
        BSP_MPU6050_Init();
    else
        failed();
}

static void recover_bus(uint32_t now)
{
    uint32_t age = (uint32_t)(now - phase_tick);

    if (recovery_step == RECOVERY_WAIT)
    {
        GPIO_InitTypeDef pins = {0};

        if (age < BSP_MPU6050_RETRY_MS)
            return;
        if (HAL_I2C_DeInit(&hi2c1) != HAL_OK)
        {
            failed();
            return;
        }
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6 | GPIO_PIN_7, GPIO_PIN_SET);
        pins.Pin = GPIO_PIN_6 | GPIO_PIN_7;
        pins.Mode = GPIO_MODE_OUTPUT_OD;
        pins.Pull = GPIO_NOPULL;
        pins.Speed = GPIO_SPEED_FREQ_HIGH;
        HAL_GPIO_Init(GPIOB, &pins);
        recovery_pulses = 0U;
        recovery_step = RECOVERY_CLOCK_HIGH;
        phase_tick = now;
        return;
    }
    /* One GPIO edge per poll slot. Keep serving UART, UI and alarms between edges. */
    if (age < BSP_MPU6050_BUS_EDGE_MS)
        return;
    switch (recovery_step)
    {
    case RECOVERY_CLOCK_HIGH:
    case RECOVERY_STOP_HIGH:
        if (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_6) == GPIO_PIN_RESET)
        {
            if (age >= BSP_MPU6050_SCL_TIMEOUT_MS)
                finish_recovery(0U);
            return;
        }
        if (recovery_step == RECOVERY_STOP_HIGH)
        {
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, GPIO_PIN_SET);
            recovery_step = RECOVERY_RELEASE;
        }
        else
        {
            uint8_t released = (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_7) == GPIO_PIN_SET);
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_RESET);
            if (released || (recovery_pulses == 9U))
            {
                HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, GPIO_PIN_RESET);
                recovery_step = RECOVERY_STOP_LOW;
            }
            else
                recovery_step = RECOVERY_CLOCK_LOW;
        }
        break;
    case RECOVERY_CLOCK_LOW:
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_SET);
        ++recovery_pulses;
        recovery_step = RECOVERY_CLOCK_HIGH;
        break;
    case RECOVERY_STOP_LOW:
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_SET);
        recovery_step = RECOVERY_STOP_HIGH;
        break;
    case RECOVERY_RELEASE:
        finish_recovery((HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_6) == GPIO_PIN_SET) &&
                        (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_7) == GPIO_PIN_SET));
        return;
    default:
        return;
    }
    phase_tick = now;
}

static int32_t rounded(double value)
{
    return (int32_t)(value + ((value >= 0.0) ? 0.5 : -0.5));
}

void BSP_MPU6050_Init(void)
{
    irq_armed = 0U;
    data_ready_irq = data_ready_tick = handled_irq = 0U;
    state = BSP_MPU6050_STARTING;
    phase_tick = HAL_GetTick();
    last_data_tick = phase_tick;
    recovery_step = RECOVERY_WAIT;
    start_phase = 0U;
    calibration_samples = 0U;
    memset(&sample, 0, sizeof(sample));
    memset(&data, 0, sizeof(data));
    memset(gyro_sum, 0, sizeof(gyro_sum));
    memset(gyro_bias, 0, sizeof(gyro_bias));
    mpu6050_filter_reset();
}

void BSP_MPU6050_DataReadyIRQ(void)
{
    if (irq_armed)
    {
        data_ready_tick = HAL_GetTick();
        ++data_ready_irq;
    }
}

uint8_t BSP_MPU6050_Poll(void)
{
    uint32_t now = HAL_GetTick();
    uint32_t primask, irq, irq_tick;
    uint8_t result;
    unsigned int i;

    if (state == BSP_MPU6050_ERROR)
    {
        recover_bus(now);
        return 0U;
    }
    if (state == BSP_MPU6050_STARTING)
    {
        if ((uint32_t)(now - phase_tick) < ((start_phase == 2U) ? BSP_MPU6050_SETTLE_MS : BSP_MPU6050_START_DELAY_MS))
            return 0U;
        if (start_phase == 0U)
        {
            if (mpu6050_reset(&hi2c1) == 0U)
            {
                failed();
                return 0U;
            }
        }
        else if (start_phase == 1U)
        {
            if (mpu6050_configure(&hi2c1) == 0U)
            {
                failed();
                return 0U;
            }
        }
        else
        {
            __HAL_GPIO_EXTI_CLEAR_IT(MPU6050_INT_Pin);
            irq_armed = 1U;
            if (!mpu6050_enable_data_ready(&hi2c1))
            {
                failed();
                return 0U;
            }
            state = BSP_MPU6050_CALIBRATING;
            last_data_tick = HAL_GetTick();
        }
        ++start_phase;
        phase_tick = HAL_GetTick();
        return 0U;
    }

    /* Snapshot the ISR-owned pair together. An IRQ arriving during I2C remains pending. */
    primask = __get_PRIMASK();
    __disable_irq();
    irq = data_ready_irq;
    irq_tick = data_ready_tick;
    __set_PRIMASK(primask);
    now = HAL_GetTick();
    if (irq == handled_irq)
    {
        if ((uint32_t)(now - last_data_tick) >= BSP_MPU6050_SAMPLE_TIMEOUT_MS)
            failed();
        return 0U;
    }
    handled_irq = irq;
    if ((uint32_t)(now - irq_tick) >= BSP_MPU6050_SAMPLE_TIMEOUT_MS)
    {
        failed();
        return 0U;
    }
    result = mpu6050_read_all(&hi2c1, &data);
    if (result == 2U)
    {
        failed();
        return 0U;
    }
    if (result == 0U)
    {
        if ((uint32_t)(HAL_GetTick() - last_data_tick) >= BSP_MPU6050_SAMPLE_TIMEOUT_MS)
            failed();
        return 0U;
    }
    now = irq_tick;
    last_data_tick = now;
    if (state == BSP_MPU6050_CALIBRATING)
    {
        for (i = 0U; i < 3U; ++i)
            gyro_sum[i] += data.gyro_raw[i];
        if (++calibration_samples == BSP_MPU6050_CALIBRATION_SAMPLES)
        {
            for (i = 0U; i < 3U; ++i)
            {
                gyro_bias[i] = (double)gyro_sum[i] / (BSP_MPU6050_CALIBRATION_SAMPLES * 131.0);
                sample.bias_mdps[i] = rounded(gyro_bias[i] * 1000.0);
            }
            state = BSP_MPU6050_READY;
            mpu6050_filter_reset();
        }
        return 0U;
    }

    for (i = 0U; i < 3U; ++i)
    {
        data.gyro_dps[i] -= gyro_bias[i];
        sample.accel_mg[i] = rounded(data.accel_g[i] * 1000.0);
        sample.gyro_mdps[i] = rounded(data.gyro_dps[i] * 1000.0);
    }
    mpu6050_filter_update(&data, now);
    sample.angle_cdeg[0] = rounded(data.angle_x * 100.0);
    sample.angle_cdeg[1] = rounded(data.angle_y * 100.0);
    sample.temperature_cdeg = rounded(data.temperature_c * 100.0);
    sample.interval_ms = (sample.samples != 0U) ? (uint32_t)(now - sample.tick) : 0U;
    if (sample.interval_ms > sample.max_interval_ms)
        sample.max_interval_ms = sample.interval_ms;
    sample.tick = now;
    sample.irq_count = irq;
    ++sample.samples;
    return 1U;
}

bsp_mpu6050_state_t BSP_MPU6050_GetState(void)
{
    return state;
}

const bsp_mpu6050_sample_t *BSP_MPU6050_GetSample(void)
{
    return &sample;
}
