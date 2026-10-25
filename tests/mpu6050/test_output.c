#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "app_ui.h"
#include "app_voice.h"
#include "debug_config.h"
#include "bsp_mpu6050.h"
#include "bsp_gps.h"
#include "bsp_key.h"
#include "bsp_led.h"
static uint32_t now;
static bsp_mpu6050_sample_t sensor;
static bsp_mpu6050_state_t mock_state;
static bsp_gps_data_t gps;
static uint8_t pressed, ui_alarm, voice_alarm;
static unsigned int voice_polls;
void app_voice_init(void) {voice_polls=0;voice_alarm=0;}
void app_voice_poll(uint8_t alarm_active) {++voice_polls;voice_alarm=alarm_active;}
static bsp_led_mode_t led_mode;
static unsigned int a_frames, g_frames, rp_frames, gps_lines, gps_polls, print_calls;
static unsigned long frame_n;
uint32_t HAL_GetTick(void) {return now;}
void BSP_LED_Init(void) {}
void BSP_LED_Poll(void) {}
void BSP_LED_SetMode(bsp_led_mode_t mode) {led_mode=mode;}
void BSP_KEY_Init(void) {}
uint8_t BSP_KEY_Scan(void) {uint8_t result=pressed;pressed=0;return result;}
void app_ui_init(void) {}
void app_ui_poll(void) {}
void app_ui_next_page(void) {}
void app_ui_set_alarm(uint8_t active) {ui_alarm=active;}
void BSP_GPS_Init(void) {}
void BSP_GPS_Poll(void) {++gps_polls;}
const bsp_gps_data_t *BSP_GPS_GetData(void) {return &gps;}
void BSP_MPU6050_Init(void) {memset(&sensor,0,sizeof(sensor));mock_state=BSP_MPU6050_STARTING;}
bsp_mpu6050_state_t BSP_MPU6050_GetState(void) {return mock_state;}
const bsp_mpu6050_sample_t *BSP_MPU6050_GetSample(void) {return &sensor;}
uint8_t BSP_MPU6050_Poll(void) {
 if(mock_state!=BSP_MPU6050_READY)return 0;
 ++sensor.samples;sensor.tick=now;sensor.interval_ms=10;sensor.max_interval_ms=10;
 sensor.accel_mg[0]=(int32_t)sensor.samples;sensor.gyro_mdps[0]=(int32_t)sensor.samples*10;
 sensor.angle_cdeg[0]=(int32_t)sensor.samples*2;return 1;
}
static int test_printf(const char *format,...) {
 char text[256];va_list args;int n;unsigned long seq;long value;
 va_start(args,format);n=vsnprintf(text,sizeof(text),format,args);va_end(args);
 assert(n>=0 && (unsigned int)n<sizeof(text));
 ++print_calls;
 if(sscanf(text,"[MPU] n=%lu A mg=%ld",&seq,&value)==2) {
  assert(DEBUG_LOG_MPU_ACCEL && value==(long)seq);frame_n=seq;++a_frames;
 } else if(sscanf(text,"[MPU] n=%lu G mdps=%ld",&seq,&value)==2) {
  assert(DEBUG_LOG_MPU_GYRO && value==(long)seq*10);
  if(DEBUG_LOG_MPU_ACCEL)assert(seq==frame_n);else frame_n=seq;
  ++g_frames;
 } else if(sscanf(text,"[MPU] n=%lu Roll=%ld",&seq,&value)==2) {
  assert(DEBUG_LOG_MPU_ANGLES && value==(long)seq*2);
  if(DEBUG_LOG_MPU_ACCEL || DEBUG_LOG_MPU_GYRO)assert(seq==frame_n);else frame_n=seq;
  ++rp_frames;
 }
 if(strstr(text,"[C05] GPS") || strstr(text,"[C05] GSV")) {
  assert((DEBUG_LOG_GPS_FIX && strstr(text,"GPS fix=")) ||
         (DEBUG_LOG_GPS_POSITION && strstr(text,"GPS lat_udeg=")) ||
         (DEBUG_LOG_GPS_UTC && strstr(text,"GPS UTC=")) ||
         (DEBUG_LOG_GPS_ALTITUDE && strstr(text,"GPS alt_cm=")) ||
         (DEBUG_LOG_GPS_STATS && (strstr(text,"GPS rx=") || strstr(text,"GPS crc="))) ||
         (DEBUG_LOG_GPS_SIGNAL && strstr(text,"GSV view=")));
  ++gps_lines;
 }
 return n;
}
#define printf test_printf
#include "../../App/app_main.c"
#undef printf
int main(void) {
 unsigned int i, before;
 app_init();mock_state=BSP_MPU6050_READY;
 for(i=0;i<240;++i){now+=10;app_run();}
 while(mpu_log_phase){now+=10;app_run();}
 assert(gps_polls>=240 && sensor.samples>=240 && voice_polls==gps_polls);
 assert(DEBUG_OUTPUT_ENABLE && DEBUG_LOG_MPU_ACCEL ? a_frames>=10 : a_frames==0);
 assert(DEBUG_OUTPUT_ENABLE && DEBUG_LOG_MPU_GYRO ? g_frames>=10 : g_frames==0);
 assert(DEBUG_OUTPUT_ENABLE && DEBUG_LOG_MPU_ANGLES ? rp_frames>=10 : rp_frames==0);
 if(DEBUG_OUTPUT_ENABLE && DEBUG_LOG_MPU_ACCEL && DEBUG_LOG_MPU_GYRO)assert(g_frames==a_frames);
 if(DEBUG_OUTPUT_ENABLE && DEBUG_LOG_MPU_ACCEL && DEBUG_LOG_MPU_ANGLES)assert(rp_frames==a_frames);
 if(DEBUG_OUTPUT_ENABLE && (DEBUG_LOG_GPS_FIX || DEBUG_LOG_GPS_POSITION || DEBUG_LOG_GPS_UTC ||
    DEBUG_LOG_GPS_ALTITUDE || DEBUG_LOG_GPS_STATS || DEBUG_LOG_GPS_SIGNAL))assert(gps_lines>0);
 else assert(gps_lines==0);
 if(DEBUG_OUTPUT_ENABLE && DEBUG_LOG_GPS_UTC && !DEBUG_LOG_MPU_ACCEL && !DEBUG_LOG_MPU_GYRO && !DEBUG_LOG_MPU_ANGLES)
    assert(gps_lines>=9);
 before=a_frames;mock_state=BSP_MPU6050_ERROR;
 for(i=0;i<30;++i){now+=10;app_run();}
 assert(a_frames==before && gps_polls>=270);
 pressed=BSP_KEY2_PRESSED;now+=10;app_run();
 assert(voice_alarm && ui_alarm && led_mode==BSP_LED_ALARM && app_fall_get_data()->source==APP_FALL_SOURCE_MANUAL);
 pressed=BSP_KEY2_PRESSED;now+=10;app_run();
 assert(!voice_alarm && !ui_alarm && led_mode==BSP_LED_OFFLINE && app_fall_get_data()->state==APP_FALL_WAIT_UPRIGHT);
 if(!DEBUG_OUTPUT_ENABLE)assert(print_calls==0);
 puts("PASS debug filtering, enabled-field cadence, coherent snapshots, sensor/GPS polling and error state");
 return 0;
}
