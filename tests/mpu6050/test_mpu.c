#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "bsp_mpu6050.h"
#include "mpu6050.h"

I2C_HandleTypeDef hi2c1;
static uint32_t tick;
static uint8_t registers[128];
static int fail_reg = -1;
static unsigned int bursts;
uint32_t HAL_GetTick(void) { return tick; }
uint32_t HAL_I2C_GetError(I2C_HandleTypeDef *h) { return h->ErrorCode; }
HAL_StatusTypeDef HAL_I2C_Mem_Read(I2C_HandleTypeDef *h, uint16_t addr, uint16_t reg,
                                uint16_t size, uint8_t *out, uint16_t count, uint32_t timeout)
{
    assert(h == &hi2c1 && addr == 0xD0 && size == 1 && timeout == 5);
    if ((int)reg == fail_reg) return HAL_ERROR;
    assert(reg + count <= sizeof(registers));
    memcpy(out, registers + reg, count);
    if (reg == 0x3A) registers[reg] = 0;
    if (reg == 0x3B) ++bursts;
    return HAL_OK;
}
HAL_StatusTypeDef HAL_I2C_Mem_Write(I2C_HandleTypeDef *h, uint16_t addr, uint16_t reg,
                                 uint16_t size, uint8_t *in, uint16_t count, uint32_t timeout)
{
    assert(h == &hi2c1 && addr == 0xD0 && size == 1 && timeout == 5 && count == 1);
    if ((int)reg == fail_reg) return HAL_ERROR;
    registers[reg] = *in;
    return HAL_OK;
}
static void word(unsigned int reg, int value)
{
    registers[reg] = (uint8_t)((uint16_t)value >> 8);
    registers[reg+1] = (uint8_t)value;
}
static void raw(int ax, int ay, int az, int gx, int gy, int gz)
{
    registers[0x3A] = 1;
    word(0x3B, ax);word(0x3D, ay);word(0x3F, az);word(0x41, 340);
    word(0x43, gx);word(0x45, gy);word(0x47, gz);
}
static void boot(void)
{
    unsigned int i;
    memset(registers, 0, sizeof(registers));registers[0x75] = 0x68;fail_reg = -1;
    BSP_MPU6050_Init();
    tick += 100;BSP_MPU6050_Poll();tick += 100;BSP_MPU6050_Poll();
    tick += 250;BSP_MPU6050_Poll();
    assert(BSP_MPU6050_GetState() == BSP_MPU6050_CALIBRATING);
    assert(registers[0x19] == 9 && registers[0x1A] == 3);
    assert(registers[0x1B] == 0 && registers[0x1C] == 0);
    for (i = 0; i < 200; ++i)
    {
        tick += 10;raw(0, 0, 16384, 131, -262, 393);
        assert(!BSP_MPU6050_Poll());
    }
    assert(BSP_MPU6050_GetState() == BSP_MPU6050_READY);
}
static void near_value(double a, double b) { assert(fabs(a-b) < 0.001); }
int main(void)
{
    mpu6050_data_t m, before;
    const bsp_mpu6050_sample_t *s;
    unsigned int i, old_bursts;
    memset(registers, 0, sizeof(registers));registers[0x75] = 0x69;
    assert(!mpu6050_configure(&hi2c1));
    registers[0x75] = 0x68;assert(mpu6050_configure(&hi2c1));
    fail_reg = 0x1B;assert(!mpu6050_configure(&hi2c1));fail_reg = -1;
    raw(-32768, 32767, 16384, -131, 262, -393);
    assert(mpu6050_read_all(&hi2c1, &m) == 1);
    near_value(m.accel_g[0], -2);near_value(m.accel_g[1], 32767.0/16384);near_value(m.accel_g[2], 1);
    near_value(m.gyro_dps[0], -1);near_value(m.gyro_dps[1], 2);near_value(m.gyro_dps[2], -3);
    before = m;fail_reg = 0x3B;registers[0x3A] = 1;
    assert(mpu6050_read_all(&hi2c1, &m) == 2 && !memcmp(&m, &before, sizeof(m)));fail_reg = -1;
    puts("PASS identity/configuration, signed range conversion and failed read does not publish data");

    memset(&m, 0, sizeof(m));m.accel_g[1] = 1;
    mpu6050_filter_reset();mpu6050_filter_update(&m, 90000);near_value(m.angle_x, 90);
    m.accel_g[1] = -1;mpu6050_filter_reset();mpu6050_filter_update(&m, 90010);near_value(m.angle_x, -90);
    m.accel_g[0] = -0.6123724357;m.accel_g[1] = 0.5;m.accel_g[2] = 0.6123724357;
    mpu6050_filter_reset();mpu6050_filter_update(&m, 0xFFFFFFF0U);
    near_value(m.angle_x, 30);near_value(m.angle_y, 45);
    for(i = 0; i < 1000; ++i)mpu6050_filter_update(&m, (uint32_t)(0xFFFFFFF0U + 10U*(i+1U)));
    near_value(m.angle_x, 30);near_value(m.angle_y, 45);
    m.accel_g[0] = 0.01;m.accel_g[1] = 0;m.accel_g[2] = -1;m.gyro_dps[0] = 5;
    mpu6050_filter_reset();mpu6050_filter_update(&m, 100);
    m.accel_g[0] = -0.01;mpu6050_filter_update(&m, 110);
    assert(m.angle_y > 179 && m.gyro_dps[0] == 5);
    puts("PASS initial attitude, +/-90 roll, mixed pose, pitch seam and tick wrap");

    boot();s = BSP_MPU6050_GetSample();
    assert(s->samples == 0 && s->bias_mdps[0] == 1000 && s->bias_mdps[1] == -2000 && s->bias_mdps[2] == 3000);
    tick += 10;raw(0, 0, 16384, 131, -262, 393);assert(BSP_MPU6050_Poll());
    assert(s->samples == 1 && s->accel_mg[2] == 1000 && s->gyro_mdps[0] == 0);
    assert(s->angle_cdeg[0] == 0 && s->angle_cdeg[1] == 0 && s->temperature_cdeg == 3753);
    old_bursts = bursts;tick += 2;assert(!BSP_MPU6050_Poll() && bursts == old_bursts && s->samples == 1);
    tick += 8;raw(8192, 0, 16384, 262, -262, 524);assert(BSP_MPU6050_Poll());
    assert(s->accel_mg[0] == 500 && s->gyro_mdps[0] == 1000 && s->gyro_mdps[2] == 1000 && s->interval_ms == 10);
    fail_reg = 0x3A;tick += 10;assert(!BSP_MPU6050_Poll());
    assert(BSP_MPU6050_GetState() == BSP_MPU6050_ERROR && s->samples == 2);
    fail_reg = -1;tick += 10;raw(0,0,16384,0,0,0);assert(!BSP_MPU6050_Poll());
    puts("PASS nonblocking startup, 200-sample gyro bias, new-data-only sampling and bounded error state");
    tick = 0xFFFFFF00U;boot();tick += 10;raw(0,0,16384,131,-262,393);assert(BSP_MPU6050_Poll());
    assert(s->samples == 1 && s->accel_mg[2] == 1000);
    puts("PASS driver startup across tick wrap");
    return 0;
}
