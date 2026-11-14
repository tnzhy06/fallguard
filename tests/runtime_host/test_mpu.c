#include "bsp_mpu6050.h"
#include "app_fall.h"
#include "mpu6050.h"
#include "i2c.h"
#include "main.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

I2C_HandleTypeDef hi2c1;
static uint32_t ticks;
static uint8_t reset_ok, configure_ok, read_result, scl_stuck, sda_stuck, gpio_mode;
static HAL_StatusTypeDef init_result, deinit_result;
static uint16_t output_pins;
static unsigned init_calls, deinit_calls, reset_calls, filter_resets, clocks, release_after;
static unsigned read_calls, enable_calls, exti_clears;
static uint8_t enable_ok, emit_irqs, irq_during_read, irq_on_enable, irq_before_snapshot;
static uint32_t irq_mask, filter_tick;

uint32_t HAL_GetTick(void) { return ticks; }
uint32_t __get_PRIMASK(void)
{
    if (irq_before_snapshot)
    {
        irq_before_snapshot = 0U;
        ++ticks;
        BSP_MPU6050_DataReadyIRQ();
    }
    return irq_mask;
}
void __disable_irq(void) { irq_mask = 1U; }
void __set_PRIMASK(uint32_t mask) { irq_mask = mask; }
void host_clear_exti(uint16_t pin)
{
    assert(pin == MPU6050_INT_Pin);
    ++exti_clears;
}

HAL_StatusTypeDef HAL_I2C_Init(I2C_HandleTypeDef *i2c)
{
    assert(i2c == &hi2c1);
    ++init_calls;
    gpio_mode = 0U; /* Emulate MSP restoring AF open-drain. */
    return init_result;
}

HAL_StatusTypeDef HAL_I2C_DeInit(I2C_HandleTypeDef *i2c)
{
    assert(i2c == &hi2c1);
    ++deinit_calls;
    return deinit_result;
}

void HAL_GPIO_Init(void *port, GPIO_InitTypeDef *pins)
{
    assert(port == GPIOB && pins->Pin == (GPIO_PIN_6 | GPIO_PIN_7));
    assert(pins->Mode == GPIO_MODE_OUTPUT_OD);
    gpio_mode = 1U;
}

void HAL_GPIO_WritePin(void *port, uint16_t pins, GPIO_PinState level)
{
    assert(port == GPIOB);
    uint16_t previous = output_pins;
    if (level == GPIO_PIN_SET) output_pins |= pins;
    else output_pins &= (uint16_t)~pins;
    if (gpio_mode && (previous & GPIO_PIN_6) == 0U && (output_pins & GPIO_PIN_6) != 0U)
    {
        ++clocks;
        if (release_after != 0U && clocks >= release_after) sda_stuck = 0U;
    }
}

GPIO_PinState HAL_GPIO_ReadPin(void *port, uint16_t pin)
{
    assert(port == GPIOB);
    if ((pin == GPIO_PIN_6 && scl_stuck) || (pin == GPIO_PIN_7 && sda_stuck))
        return GPIO_PIN_RESET;
    return (output_pins & pin) != 0U ? GPIO_PIN_SET : GPIO_PIN_RESET;
}

uint8_t mpu6050_reset(I2C_HandleTypeDef *i2c)
{
    assert(i2c == &hi2c1);
    ++reset_calls;
    return reset_ok;
}

uint8_t mpu6050_configure(I2C_HandleTypeDef *i2c)
{
    assert(i2c == &hi2c1);
    return configure_ok;
}

uint8_t mpu6050_enable_data_ready(I2C_HandleTypeDef *i2c)
{
    assert(i2c == &hi2c1);
    ++enable_calls;
    if (irq_on_enable) BSP_MPU6050_DataReadyIRQ();
    return enable_ok;
}

uint8_t mpu6050_read_all(I2C_HandleTypeDef *i2c, mpu6050_data_t *data)
{
    assert(i2c == &hi2c1);
    ++read_calls;
    if (irq_during_read)
    {
        irq_during_read = 0U;
        ++ticks;
        BSP_MPU6050_DataReadyIRQ();
    }
    memset(data, 0, sizeof(*data));
    data->gyro_raw[0] = 131;
    data->gyro_dps[0] = 1.0;
    data->accel_g[2] = 1.0;
    return read_result;
}

void mpu6050_filter_reset(void) { ++filter_resets; }
void mpu6050_filter_update(mpu6050_data_t *data, uint32_t tick)
{
    filter_tick = tick;
    data->angle_x = 70.0;
}

static void advance(uint32_t ms)
{
    ticks += ms;
    if (emit_irqs && read_result != 0U) BSP_MPU6050_DataReadyIRQ();
    BSP_MPU6050_Poll();
    app_fall_poll();
}

static void reset(void)
{
    ticks = init_calls = deinit_calls = reset_calls = filter_resets = clocks = release_after = 0U;
    read_calls = enable_calls = exti_clears = irq_mask = filter_tick = 0U;
    enable_ok = emit_irqs = 1U;
    irq_during_read = irq_on_enable = irq_before_snapshot = 0U;
    reset_ok = configure_ok = read_result = 1U;
    scl_stuck = sda_stuck = gpio_mode = 0U;
    output_pins = GPIO_PIN_6 | GPIO_PIN_7;
    init_result = deinit_result = HAL_OK;
    BSP_MPU6050_Init();
    app_fall_init();
}

static void start_calibration(void)
{
    advance(100U);
    advance(100U);
    advance(250U);
    assert(BSP_MPU6050_GetState() == BSP_MPU6050_CALIBRATING);
}

static void finish_calibration(void)
{
    for (unsigned i = 0U; i < 200U; ++i) advance(10U);
    assert(BSP_MPU6050_GetState() == BSP_MPU6050_READY);
    assert(BSP_MPU6050_GetSample()->samples == 0U);
    advance(10U);
    assert(BSP_MPU6050_GetSample()->samples == 1U);
    assert(BSP_MPU6050_GetSample()->bias_mdps[0] == 1000);
    assert(BSP_MPU6050_GetSample()->gyro_mdps[0] == 0);
}

static void run_recovery(void)
{
    advance(BSP_MPU6050_RETRY_MS);
    for (unsigned i = 0U; i < 50U; ++i) advance(1U);
}

static void test_read_fault_recalibrates_and_resets_hold(void)
{
    reset();
    start_calibration();
    finish_calibration();
    advance(100U);
    assert(app_fall_get_data()->state == APP_FALL_HOLDING);
    read_result = 2U;
    advance(10U);
    assert(BSP_MPU6050_GetState() == BSP_MPU6050_ERROR);
    assert(BSP_MPU6050_GetSample()->samples == 0U);
    assert(app_fall_get_data()->state == APP_FALL_IDLE);
    advance(BSP_MPU6050_RETRY_MS - 1U);
    assert(deinit_calls == 0U);
    advance(1U);
    assert(deinit_calls == 1U);
    for (unsigned i = 0U; i < 50U; ++i) advance(1U);
    assert(BSP_MPU6050_GetState() == BSP_MPU6050_STARTING && init_calls == 1U);
    assert(gpio_mode == 0U);
    read_result = 1U;
    start_calibration();
    finish_calibration();
    assert(app_fall_get_data()->state == APP_FALL_HOLDING);
    assert(app_fall_get_data()->held_ms == 0U);
    for (unsigned i = 0U; i < 99U; ++i) advance(10U);
    assert(app_fall_get_data()->state == APP_FALL_HOLDING);
    advance(10U);
    assert(app_fall_get_data()->state == APP_FALL_ALARM);
    assert(reset_calls == 2U && filter_resets >= 4U);
}

static void test_start_failures_and_missing_samples(void)
{
    reset();
    reset_ok = 0U;
    advance(100U);
    assert(BSP_MPU6050_GetState() == BSP_MPU6050_ERROR);
    run_recovery();
    reset_ok = 1U;
    configure_ok = 0U;
    advance(100U);
    advance(100U);
    assert(BSP_MPU6050_GetState() == BSP_MPU6050_ERROR);
    run_recovery();
    configure_ok = 1U;
    start_calibration();
    read_result = 0U;
    advance(BSP_MPU6050_SAMPLE_TIMEOUT_MS - 1U);
    assert(BSP_MPU6050_GetState() == BSP_MPU6050_CALIBRATING);
    advance(2U);
    assert(BSP_MPU6050_GetState() == BSP_MPU6050_ERROR);
    run_recovery();
    read_result = 1U;
    start_calibration();
    finish_calibration();
    read_result = 0U;
    advance(BSP_MPU6050_SAMPLE_TIMEOUT_MS);
    assert(BSP_MPU6050_GetState() == BSP_MPU6050_ERROR);
}

static void test_stuck_lines_and_repeated_retries(void)
{
    reset();
    reset_ok = 0U;
    advance(100U);
    sda_stuck = 1U;
    release_after = 3U;
    run_recovery();
    assert(BSP_MPU6050_GetState() == BSP_MPU6050_STARTING);
    assert(clocks == 4U); /* Three release pulses followed by the STOP clock edge. */

    reset();
    reset_ok = 0U;
    advance(100U);
    sda_stuck = 1U;
    run_recovery();
    assert(BSP_MPU6050_GetState() == BSP_MPU6050_ERROR && clocks == 10U);
    sda_stuck = 0U;
    run_recovery();
    assert(BSP_MPU6050_GetState() == BSP_MPU6050_STARTING && init_calls == 2U);

    reset();
    reset_ok = 0U;
    advance(100U);
    scl_stuck = 1U;
    run_recovery();
    assert(BSP_MPU6050_GetState() == BSP_MPU6050_ERROR && init_calls == 1U);
    scl_stuck = 0U;
    run_recovery();
    assert(BSP_MPU6050_GetState() == BSP_MPU6050_STARTING && init_calls == 2U);
}

static void test_hal_failures_and_tick_wrap(void)
{
    reset();
    ticks = UINT32_MAX - 50U;
    BSP_MPU6050_Init();
    reset_ok = 0U;
    advance(100U);
    deinit_result = HAL_ERROR;
    run_recovery();
    assert(BSP_MPU6050_GetState() == BSP_MPU6050_ERROR && init_calls == 0U);
    deinit_result = HAL_OK;
    init_result = HAL_ERROR;
    run_recovery();
    assert(BSP_MPU6050_GetState() == BSP_MPU6050_ERROR && init_calls == 1U);
    init_result = HAL_OK;
    ticks = UINT32_MAX - 1000U;
    BSP_MPU6050_Init();
    advance(100U);
    run_recovery();
    assert(BSP_MPU6050_GetState() == BSP_MPU6050_STARTING && init_calls == 2U);
}

static void test_interrupt_gating_and_rearming(void)
{
    reset();
    emit_irqs = 0U;
    BSP_MPU6050_DataReadyIRQ(); /* An edge before startup must not enter calibration. */
    start_calibration();
    assert(enable_calls == 1U && exti_clears == 1U);
    for (unsigned i = 0U; i < 100U; ++i) advance(1U);
    assert(read_calls == 0U && BSP_MPU6050_GetState() == BSP_MPU6050_CALIBRATING);
    BSP_MPU6050_DataReadyIRQ();
    assert(read_calls == 0U); /* No I2C work in the ISR callback. */
    BSP_MPU6050_Poll();
    assert(read_calls == 1U);
    for (unsigned i = 0U; i < 10U; ++i) BSP_MPU6050_Poll();
    assert(read_calls == 1U); /* One event is consumed exactly once. */
    BSP_MPU6050_DataReadyIRQ();
    BSP_MPU6050_DataReadyIRQ();
    BSP_MPU6050_DataReadyIRQ();
    BSP_MPU6050_Poll();
    assert(read_calls == 2U); /* Coalesced notifications trigger one current-data read. */

    reset();
    emit_irqs = 0U;
    start_calibration();
    advance(BSP_MPU6050_SAMPLE_TIMEOUT_MS);
    assert(read_calls == 0U && BSP_MPU6050_GetState() == BSP_MPU6050_ERROR);
    BSP_MPU6050_DataReadyIRQ();
    run_recovery();
    assert(read_calls == 0U && BSP_MPU6050_GetState() == BSP_MPU6050_STARTING);
    emit_irqs = 1U;
    start_calibration();
    finish_calibration();
    assert(enable_calls == 2U && BSP_MPU6050_GetSample()->irq_count == 201U);

    reset();
    enable_ok = 0U;
    advance(100U); advance(100U); advance(250U);
    assert(BSP_MPU6050_GetState() == BSP_MPU6050_ERROR);
    enable_ok = 1U;
    run_recovery();
    start_calibration();
    finish_calibration();
}

static void test_irq_during_read_and_timestamp(void)
{
    reset();
    emit_irqs = 0U;
    start_calibration();
    irq_before_snapshot = 1U;
    BSP_MPU6050_Poll();
    assert(read_calls == 1U && BSP_MPU6050_GetState() == BSP_MPU6050_CALIBRATING);

    reset();
    emit_irqs = 0U;
    irq_on_enable = 1U;
    start_calibration();
    irq_on_enable = 0U;
    irq_during_read = 1U;
    BSP_MPU6050_Poll();
    assert(read_calls == 1U);
    BSP_MPU6050_Poll();
    assert(read_calls == 2U); /* The event raised during the first read remains pending. */
    BSP_MPU6050_Poll();
    assert(read_calls == 2U);

    reset();
    start_calibration();
    finish_calibration();
    emit_irqs = 0U;
    ticks = UINT32_MAX - 2U;
    BSP_MPU6050_DataReadyIRQ();
    uint32_t captured = ticks;
    ticks += 5U;
    BSP_MPU6050_Poll();
    assert(BSP_MPU6050_GetSample()->tick == captured && filter_tick == captured);
    unsigned before = read_calls;
    ticks += 10U;
    BSP_MPU6050_DataReadyIRQ();
    ticks += BSP_MPU6050_SAMPLE_TIMEOUT_MS;
    BSP_MPU6050_Poll();
    assert(read_calls == before && BSP_MPU6050_GetState() == BSP_MPU6050_ERROR);

    reset();
    start_calibration();
    emit_irqs = 0U;
    read_result = 0U;
    BSP_MPU6050_DataReadyIRQ(); /* A spurious edge has no DATA_RDY status. */
    BSP_MPU6050_Poll();
    assert(read_calls == 1U);
    read_result = 1U;
    emit_irqs = 1U;
    finish_calibration(); /* The spurious event must not count toward the 200 samples. */
}

int main(void)
{
    test_read_fault_recalibrates_and_resets_hold();
    test_start_failures_and_missing_samples();
    test_stuck_lines_and_repeated_retries();
    test_hal_failures_and_tick_wrap();
    test_interrupt_gating_and_rearming();
    test_irq_during_read_and_timestamp();
    puts("MPU automatic recovery tests passed");
    return 0;
}
