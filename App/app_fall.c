#include "app_fall.h"
#include "bsp_mpu6050.h"
#include "stm32f1xx_hal.h"

static app_fall_data_t data;
static uint32_t hold_tick, last_tick, last_sample, upright_tick, motion_tick;
static uint8_t have_sample, upright_pending, motion_active;

/* Start/restart only after a stable normal pose. An alarm stays latched until
 * the existing key/MQTT control explicitly cancels it. */
static void wait_upright(void) {
  data.state = APP_FALL_WAIT_UPRIGHT;
  data.source = APP_FALL_SOURCE_NONE;
  data.held_ms = 0U;
  have_sample = upright_pending = motion_active = 0U;
}

static uint32_t magnitude(int32_t value) {
  return value < 0 ? 0U - (uint32_t)value : (uint32_t)value;
}

void app_fall_init(void) {
  hold_tick = last_tick = last_sample = upright_tick = motion_tick = 0U;
  wait_upright();
}

void app_fall_toggle_alarm(void) {
  uint8_t canceled = data.state == APP_FALL_ALARM;
  wait_upright();
  if (!canceled) {
    data.state = APP_FALL_ALARM;
    data.source = APP_FALL_SOURCE_MANUAL;
  }
}

void app_fall_poll(void) {
  const bsp_mpu6050_sample_t *sample = BSP_MPU6050_GetSample();
  uint32_t tilt, pitch;

  if (data.state == APP_FALL_ALARM)
    return;
  /* Old/repeated samples cannot extend either the hold or the rearm dwell. */
  if (BSP_MPU6050_GetState() != BSP_MPU6050_READY || sample->samples == 0U ||
      (uint32_t)(HAL_GetTick() - sample->tick) >= APP_FALL_SAMPLE_GAP_MS) {
    wait_upright();
    return;
  }
  if (have_sample && sample->samples == last_sample)
    return;
  if (!have_sample ||
      (uint32_t)(sample->tick - last_tick) >= APP_FALL_SAMPLE_GAP_MS)
    wait_upright();
  last_tick = sample->tick;
  last_sample = sample->samples;
  have_sample = 1U;

  tilt = magnitude(sample->angle_cdeg[0]);
  pitch = magnitude(sample->angle_cdeg[1]);
  if (pitch > tilt)
    tilt = pitch;

  if (data.state == APP_FALL_WAIT_UPRIGHT) {
    if (tilt > APP_FALL_UPRIGHT_ANGLE_DEG * 100U) {
      upright_pending = 0U;
    } else if (!upright_pending) {
      upright_tick = sample->tick;
      upright_pending = 1U;
    } else if ((uint32_t)(sample->tick - upright_tick) >=
               APP_FALL_UPRIGHT_HOLD_MS) {
      data.state = APP_FALL_IDLE;
      upright_pending = motion_active = 0U;
    }
    return;
  }

  if (data.state == APP_FALL_HOLDING) {
    if (tilt < APP_FALL_ANGLE_DEG * 100U) {
      wait_upright();
    } else {
      data.held_ms = (uint32_t)(sample->tick - hold_tick);
      if (data.held_ms >= APP_FALL_HOLD_MS) {
        data.held_ms = APP_FALL_HOLD_MS;
        data.state = APP_FALL_ALARM;
        data.source = APP_FALL_SOURCE_AUTO;
      }
    }
    return;
  }

  /* Classroom gesture: leave the normal zone and reach the tilt threshold
   * within one short window. A slow placement times out until rearmed. */
  if (tilt <= APP_FALL_UPRIGHT_ANGLE_DEG * 100U) {
    motion_active = 0U;
    return;
  }
  if (!motion_active) {
    motion_tick = sample->tick;
    motion_active = 1U;
  }
  if ((uint32_t)(sample->tick - motion_tick) > APP_FALL_TILT_WINDOW_MS) {
    wait_upright();
    return;
  }
  if (tilt >= APP_FALL_ANGLE_DEG * 100U) {
    data.state = APP_FALL_HOLDING;
    data.held_ms = 0U;
    hold_tick = sample->tick;
    motion_active = 0U;
  }
}

const app_fall_data_t *app_fall_get_data(void) { return &data; }
