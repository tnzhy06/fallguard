#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "app_fall.h"
#include "bsp_mpu6050.h"
static uint32_t now;
static bsp_mpu6050_state_t sensor_state;
static bsp_mpu6050_sample_t sensor;
uint32_t HAL_GetTick(void) {return now;}
bsp_mpu6050_state_t BSP_MPU6050_GetState(void) {return sensor_state;}
const bsp_mpu6050_sample_t *BSP_MPU6050_GetSample(void) {return &sensor;}
static void reset(void) {
 memset(&sensor,0,sizeof(sensor));sensor_state=BSP_MPU6050_READY;now=100;app_fall_init();
}
static void sample(int32_t roll,int32_t pitch,uint32_t dt) {
 now+=dt;sensor.tick=now;++sensor.samples;sensor.angle_cdeg[0]=roll;sensor.angle_cdeg[1]=pitch;
 app_fall_poll();
}
static void hold(int32_t roll,int32_t pitch) {
 unsigned int i;
 sample(roll,pitch,10);
 for(i=0;i<100;++i)sample(roll,pitch,10);
}
int main(void) {
 const app_fall_data_t *f;unsigned int i;
 reset();f=app_fall_get_data();
 for(i=0;i<200;++i)sample(5999,-5999,10);
 assert(f->state==APP_FALL_IDLE);
 sample(6000,0,10);assert(f->state==APP_FALL_HOLDING);
 for(i=0;i<99;++i)sample(6000,0,10);
 sample(6000,0,9);assert(f->state==APP_FALL_HOLDING && f->held_ms==999);
 sample(6000,0,1);assert(f->state==APP_FALL_ALARM && f->source==APP_FALL_SOURCE_AUTO);
 sample(0,0,10);assert(f->state==APP_FALL_ALARM);
 puts("PASS 59.99/60-degree boundary, 999/1000ms hold and latched alarm after raising board");
 app_fall_toggle_alarm();assert(f->state==APP_FALL_WAIT_UPRIGHT);
 for(i=0;i<200;++i)sample(-6000,0,10);
 assert(f->state==APP_FALL_WAIT_UPRIGHT);
 sample(0,6000,10);assert(f->state==APP_FALL_WAIT_UPRIGHT);
 sample(5999,-5999,10);assert(f->state==APP_FALL_IDLE);
 hold(0,-6000);assert(f->state==APP_FALL_ALARM && f->source==APP_FALL_SOURCE_AUTO);
 puts("PASS cancel suppresses repeated alarm until BOTH axes below threshold; negative Pitch triggers");
 reset();sample(7000,0,10);
 for(i=0;i<50;++i)sample(7000,0,10);
 sample(0,0,10);assert(f->state==APP_FALL_IDLE && !f->held_ms);
 sample(-7000,0,10);
 for(i=0;i<99;++i)sample(-7000,0,10);
 assert(f->state==APP_FALL_HOLDING);
 sample(-7000,0,10);assert(f->state==APP_FALL_ALARM);
 puts("PASS short tilt reset and negative Roll trigger");
 reset();sample(0,7000,10);
 for(i=0;i<20;++i)sample(0,7000,10);
 sensor_state=BSP_MPU6050_ERROR;now+=10;app_fall_poll();
 assert(f->state==APP_FALL_IDLE);
 app_fall_toggle_alarm();assert(f->state==APP_FALL_ALARM && f->source==APP_FALL_SOURCE_MANUAL);
 now+=2000;app_fall_poll();assert(f->state==APP_FALL_ALARM);
 app_fall_toggle_alarm();assert(f->state==APP_FALL_WAIT_UPRIGHT);
 app_fall_poll();assert(f->state==APP_FALL_WAIT_UPRIGHT);
 sensor_state=BSP_MPU6050_READY;sample(0,0,10);assert(f->state==APP_FALL_IDLE);
 puts("PASS sensor error resets candidate; manual test/cancel independent of sensor availability");
 reset();sample(7000,0,10);now+=500;app_fall_poll();
 assert(f->state==APP_FALL_HOLDING && !f->held_ms);
 now+=500;app_fall_poll();assert(f->state==APP_FALL_IDLE);
 sample(7000,0,10);assert(f->state==APP_FALL_HOLDING);
 sample(7000,0,1000);assert(f->state==APP_FALL_HOLDING && !f->held_ms);
 reset();now=0xFFFFFF00U;hold(7000,0);assert(f->state==APP_FALL_ALARM);
 puts("PASS duplicate/stalled data does not trigger, full-interval gap restarts hold, tick rollover");
 return 0;
}
