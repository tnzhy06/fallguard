#include "mpu6050.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static I2C_HandleTypeDef bus;
static uint8_t registers[128];
static int fail_read, fail_write, corrupt_register;
static uint8_t corrupt_value;
static unsigned reads, writes;
static uint16_t last_read_register, last_read_size;
uint8_t host_i2c_busy;
static uint32_t ticks;

uint32_t HAL_GetTick(void) { return ticks; }
uint32_t HAL_I2C_GetError(I2C_HandleTypeDef *i2c)
{
    assert(i2c == &bus);
    return 4U;
}

HAL_StatusTypeDef HAL_I2C_Mem_Read(I2C_HandleTypeDef *i2c, uint16_t address, uint16_t reg,
                                uint16_t address_size, uint8_t *bytes, uint16_t size, uint32_t timeout)
{
    assert(i2c == &bus && address == MPU6050_ADDRESS);
    assert(address_size == I2C_MEMADD_SIZE_8BIT && timeout == MPU6050_TIMEOUT_MS);
    assert((size_t)reg + size <= sizeof(registers));
    ++reads;
    last_read_register = reg;
    last_read_size = size;
    if (reg == fail_read)
    {
        memset(bytes, 0xCC, size); /* A failed transaction may already have filled bytes. */
        return HAL_ERROR;
    }
    memcpy(bytes, registers + reg, size);
    if (corrupt_register >= reg && corrupt_register < reg + size)
        bytes[corrupt_register - reg] = corrupt_value;
    if (reg == 0x3AU) registers[0x3A] = 0U;
    return HAL_OK;
}

HAL_StatusTypeDef HAL_I2C_Mem_Write(I2C_HandleTypeDef *i2c, uint16_t address, uint16_t reg,
                                 uint16_t address_size, uint8_t *bytes, uint16_t size, uint32_t timeout)
{
    assert(i2c == &bus && address == MPU6050_ADDRESS);
    assert(address_size == I2C_MEMADD_SIZE_8BIT && timeout == MPU6050_TIMEOUT_MS);
    assert((size_t)reg + size <= sizeof(registers));
    ++writes;
    if (reg == fail_write) return HAL_ERROR;
    memcpy(registers + reg, bytes, size);
    return HAL_OK;
}

static void reset(void)
{
    memset(registers, 0, sizeof(registers));
    registers[0x75] = 0x68U;
    fail_read = fail_write = corrupt_register = -1;
    corrupt_value = host_i2c_busy = 0U;
    reads = writes = ticks = 0U;
}

static void test_latched_interrupt_configuration(void)
{
    reset();
    assert(mpu6050_reset(&bus));
    assert(registers[0x6B] == 0x80U);
    assert(mpu6050_configure(&bus));
    assert(registers[0x19] == MPU6050_SAMPLE_RATE_DIV);
    assert(registers[0x1A] == MPU6050_DLPF_CFG);
    assert(registers[0x1B] == 0U && registers[0x1C] == 0U);
    assert(registers[0x37] == 0x20U); /* Active high, push-pull, latch, status-read clear. */
    assert(registers[0x38] == 0U); /* Interrupts stay disabled during startup settling. */
    registers[0x3A] = 1U;
    assert(mpu6050_enable_data_ready(&bus));
    assert(registers[0x3A] == 0U && registers[0x38] == 1U);
}

static void set_word(unsigned reg, int16_t value)
{
    registers[reg] = (uint8_t)((uint16_t)value >> 8U);
    registers[reg + 1U] = (uint8_t)value;
}

static void test_single_burst_decode_and_failures(void)
{
    mpu6050_data_t data;
    uint8_t before[sizeof(data)];
    reset();
    registers[0x3A] = 1U;
    set_word(0x3B, -32768);
    set_word(0x3D, 16384);
    set_word(0x3F, -16384);
    set_word(0x41, 340);
    set_word(0x43, -131);
    set_word(0x45, 262);
    set_word(0x47, -32768);
    memset(&data, 0, sizeof(data));
    assert(mpu6050_read_all(&bus, &data) == 1U);
    assert(reads == 1U && last_read_register == 0x3AU && last_read_size == 15U);
    assert(registers[0x3A] == 0U);
    assert(data.accel_raw[0] == -32768 && data.accel_raw[1] == 16384 && data.accel_raw[2] == -16384);
    assert(data.gyro_raw[0] == -131 && data.gyro_raw[1] == 262 && data.gyro_raw[2] == -32768);
    assert(data.accel_g[0] == -2.0 && data.accel_g[1] == 1.0 && data.accel_g[2] == -1.0);
    assert(data.gyro_dps[0] == -1.0 && data.gyro_dps[1] == 2.0);
    assert(fabs(data.temperature_c - 37.53) < 0.000001);
    memcpy(before, &data, sizeof(data));
    assert(mpu6050_read_all(&bus, &data) == 0U);
    assert(memcmp(before, &data, sizeof(data)) == 0);
    registers[0x3A] = 1U;
    fail_read = 0x3A;
    assert(mpu6050_read_all(&bus, &data) == 2U);
    assert(memcmp(before, &data, sizeof(data)) == 0);
}

static void test_configuration_errors(void)
{
    reset(); registers[0x75] = 0U;
    assert(!mpu6050_configure(&bus));
    reset(); fail_read = 0x75;
    assert(!mpu6050_configure(&bus));
    const int write_registers[] = {0x38, 0x6B, 0x6C, 0x19, 0x1A, 0x1B, 0x1C, 0x37};
    for (unsigned i = 0U; i < sizeof(write_registers) / sizeof(write_registers[0]); ++i)
    {
        reset(); fail_write = write_registers[i];
        assert(!mpu6050_configure(&bus));
    }
    reset(); corrupt_register = 0x37; corrupt_value = 0U;
    assert(!mpu6050_configure(&bus));
    reset(); corrupt_register = 0x38; corrupt_value = 1U;
    assert(!mpu6050_configure(&bus));
    reset(); fail_read = 0x3A;
    assert(!mpu6050_enable_data_ready(&bus));
    reset(); fail_write = 0x38;
    assert(!mpu6050_enable_data_ready(&bus));
    reset(); fail_read = 0x38;
    assert(!mpu6050_enable_data_ready(&bus));
    reset(); corrupt_register = 0x38; corrupt_value = 0U;
    assert(!mpu6050_enable_data_ready(&bus));
}

static void test_upstream_filter_seeding(void)
{
    mpu6050_data_t data = {0};
    data.accel_g[1] = 1.0;
    mpu6050_filter_reset();
    mpu6050_filter_update(&data, 100U);
    assert(fabs(data.angle_x - 90.0) < 0.000001);
    assert(fabs(data.angle_y) < 0.000001);
    memset(&data, 0, sizeof(data));
    data.accel_g[2] = 1.0;
    mpu6050_filter_reset();
    mpu6050_filter_update(&data, UINT32_MAX - 5U);
    mpu6050_filter_update(&data, 4U);
    assert(fabs(data.angle_x) < 0.000001 && fabs(data.angle_y) < 0.000001);
}

int main(void)
{
    test_latched_interrupt_configuration();
    test_single_burst_decode_and_failures();
    test_configuration_errors();
    test_upstream_filter_seeding();
    puts("MPU6050 open-source INT driver tests passed");
    return 0;
}
