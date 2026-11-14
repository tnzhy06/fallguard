/* Host integration test: the real driver, rasterizer and application UI.
 * HAL mocks emulate the SPI panel and delayed DMA, including buffer ownership.
 */
#include "main.h"
#include "st7789.h"
#include "bsp_lcd.h"
#include "app_ui.h"
#include "app_audio.h"
#include "app_fall.h"
#include "app_report.h"
#include "bsp_gps.h"
#include "bsp_mpu6050.h"
#include "bsp_esp.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

SPI_HandleTypeDef hspi1;
static uint32_t ticks, dma_started, dma_hash;
static uint8_t *dma_pixels;
static uint16_t dma_length, window_y, window_end;
static uint8_t screen[240 * 240 * 3];
static unsigned row_writes[240], transfers, aborts, cmd_count, inject_error, stall_dma;
static GPIO_PinState cs, dc, backlight;
static uint8_t current_cmd;
static bsp_rtc_data_t rtc;
static bsp_gps_data_t gps;
static bsp_mpu6050_sample_t mpu;
static bsp_mpu6050_state_t mpu_state;
static bsp_esp_data_t esp;
static app_report_data_t report;
static app_fall_data_t fall;
static app_audio_state_t audio;

const bsp_rtc_data_t *BSP_RTC_GetData(void) { return &rtc; }
const bsp_gps_data_t *BSP_GPS_GetData(void) { return &gps; }
const bsp_mpu6050_sample_t *BSP_MPU6050_GetSample(void) { return &mpu; }
bsp_mpu6050_state_t BSP_MPU6050_GetState(void) { return mpu_state; }
const bsp_esp_data_t *BSP_ESP_GetData(void) { return &esp; }
uint8_t BSP_ESP_IsOnline(void) { return esp.mqtt_connected && esp.mqtt_subscribed; }
const app_report_data_t *app_report_get_data(void) { return &report; }
const app_fall_data_t *app_fall_get_data(void) { return &fall; }
app_audio_state_t app_audio_get_state(void) { return audio; }
uint32_t HAL_GetTick(void) { return ticks; }

static uint32_t hash_bytes(const uint8_t *p, unsigned length)
{
    uint32_t h = 2166136261U;
    for (unsigned i = 0; i < length; ++i) h = (h ^ p[i]) * 16777619U;
    return h;
}

void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState level)
{
    (void)port;
    if (pin == LCD_CS_Pin) cs = level;
    if (pin == LCD_DC_Pin) dc = level;
    if (pin == LCD_BL_Pin)
    {
        if (level == GPIO_PIN_SET)
            for (unsigned i = 0; i < 240U; ++i) assert(row_writes[i] != 0U);
        backlight = level;
    }
}

HAL_StatusTypeDef HAL_SPI_Transmit(SPI_HandleTypeDef *spi, uint8_t *data, uint16_t length, uint32_t timeout)
{
    (void)spi;
    assert(cs == GPIO_PIN_RESET && timeout <= 2U);
    assert(hspi1.state == HAL_SPI_STATE_READY);
    if (inject_error) { inject_error = 0U; return HAL_ERROR; }
    if (dc == GPIO_PIN_RESET)
    {
        assert(length == 1U); current_cmd = data[0]; ++cmd_count;
    }
    else if (current_cmd == 0x2B)
    {
        assert(length == 4U);
        window_y = ((uint16_t)data[0] << 8) | data[1];
        window_end = ((uint16_t)data[2] << 8) | data[3];
    }
    return HAL_OK;
}

HAL_StatusTypeDef HAL_SPI_Transmit_DMA(SPI_HandleTypeDef *spi, uint8_t *pixels, uint16_t length)
{
    assert(cs == GPIO_PIN_RESET && dc == GPIO_PIN_SET && current_cmd == 0x2C);
    assert(spi->state == HAL_SPI_STATE_READY && !dma_pixels);
    assert(length == (window_end - window_y + 1U) * 240U * 2U);
    assert(length <= 240U * LCD_BAND_ROWS * 2U);
    dma_pixels = pixels; dma_length = length; dma_started = ticks;
    dma_hash = hash_bytes(pixels, length); spi->state = HAL_SPI_STATE_BUSY_TX;
    ++transfers;
    return HAL_OK;
}

HAL_SPI_StateTypeDef HAL_SPI_GetState(SPI_HandleTypeDef *spi) { return spi->state; }
uint32_t HAL_SPI_GetError(SPI_HandleTypeDef *spi) { return spi->error; }
HAL_StatusTypeDef HAL_SPI_Abort(SPI_HandleTypeDef *spi)
{
    dma_pixels = 0; spi->state = HAL_SPI_STATE_READY; spi->error = 0; ++aborts;
    return HAL_OK;
}

static void complete_dma(void)
{
    if (!dma_pixels) return;
    assert(hash_bytes(dma_pixels, dma_length) == dma_hash);
    if (stall_dma || ticks - dma_started < 2U) return;
    unsigned y = window_y - ST7789_Y_SHIFT;
    for (unsigned i = 0; i < dma_length / 2U; ++i)
    {
        uint16_t c = ((uint16_t)dma_pixels[i * 2U] << 8) | dma_pixels[i * 2U + 1U];
        unsigned p = (y * 240U + i) * 3U;
        assert(p + 2U < sizeof(screen));
        screen[p] = ((c >> 11) & 31U) * 255U / 31U;
        screen[p + 1U] = ((c >> 5) & 63U) * 255U / 63U;
        screen[p + 2U] = (c & 31U) * 255U / 31U;
    }
    for (unsigned i = 0; i < dma_length / 480U; ++i) ++row_writes[y + i];
    dma_pixels = 0; hspi1.state = HAL_SPI_STATE_READY;
}

static void run_ui(unsigned ms)
{
    for (unsigned i = 0; i < ms; ++i) { ++ticks; complete_dma(); app_ui_poll(); }
}

static void save(const char *name)
{
    char path[120];
    snprintf(path, sizeof(path), "%s.ppm", name);
    FILE *f = fopen(path, "wb"); assert(f);
    fprintf(f, "P6\n240 240\n255\n");
    assert(fwrite(screen, 1, sizeof(screen), f) == sizeof(screen)); fclose(f);
}

int main(void)
{
    /* RGB565 wire order and clipping at all strip edges. */
    BSP_LCD_BeginBand(0, 0xF800);
    assert(BSP_LCD_GetBand()[0] == 0xF8 && BSP_LCD_GetBand()[1] == 0x00);
    BSP_LCD_Rect(-4, -4, 8, 8, 0x001F);
    assert(BSP_LCD_GetBand()[0] == 0x00 && BSP_LCD_GetBand()[1] == 0x1F);
    BSP_LCD_BeginBand(232, 0x07E0);
    BSP_LCD_Rect(239, 239, 50, 50, 0xFFFF);
    assert(BSP_LCD_GetBand()[3838] == 0xFF && BSP_LCD_GetBand()[3839] == 0xFF);

    rtc.valid = 1; rtc.local.year = 2026; rtc.local.month = 11; rtc.local.day = 5;
    rtc.local.hour = 14; rtc.local.minute = 28; rtc.local.second = 36;
    gps.fix = gps.has_position = gps.signal_valid = gps.hdop_valid = gps.utc_valid = 1;
    gps.latitude_udeg = 31123456; gps.longitude_udeg = 121456789; gps.altitude_cm = 1850;
    gps.signal_level = BSP_GPS_SIGNAL_STRONG; gps.signal_max_dbhz = 42;
    gps.satellites = 12; gps.satellites_in_view = 24; gps.hdop_x100 = 85;
    gps.utc_hour = 6; gps.utc_minute = 28; gps.utc_second = 36; gps.utc_month = 11; gps.utc_day = 5;
    mpu_state = BSP_MPU6050_READY; mpu.samples = 100; mpu.accel_mg[2] = 999;
    mpu.angle_cdeg[0] = 235; mpu.angle_cdeg[1] = -112;
    esp.mqtt_connected = esp.mqtt_subscribed = esp.wifi_connected = 1;
    esp.state = BSP_ESP_MQTT_ONLINE; esp.published = 128; report.interval_seconds = 5;
    app_ui_init(); run_ui(900);
    assert(backlight == GPIO_PIN_SET && ST7789_GetState() == ST7789_READY);
    save("home");
    const char *names[] = {"position", "satellite", "motion", "network"};
    for (unsigned i = 0; i < 4U; ++i) { app_ui_next_page(); run_ui(250); save(names[i]); }
    /* Interrupt an in-progress frame with an alarm and later cancel it. */
    fall.source = APP_FALL_SOURCE_AUTO; audio = APP_AUDIO_PLAYING;
    app_ui_set_alarm(1); run_ui(250); save("alarm");
    app_ui_next_page(); run_ui(250); save("alarm_k1");
    fall.source = APP_FALL_SOURCE_MANUAL; audio = APP_AUDIO_ERROR;
    run_ui(250); save("test_alarm");
    app_ui_set_alarm(0); run_ui(250); save("cancel_home");
    esp.mqtt_connected = esp.wifi_connected = 0; esp.state = BSP_ESP_BACKOFF;
    gps.fix = gps.signal_valid = 0; gps.age_ms = 65000;
    rtc.valid = 0; mpu_state = BSP_MPU6050_CALIBRATING;
    run_ui(250); save("calibration");
    app_ui_next_page(); run_ui(250); save("last_position");
    app_ui_next_page(); run_ui(250); save("weak_signal");
    app_ui_next_page(); run_ui(250); save("calibration_motion");
    mpu_state = BSP_MPU6050_ERROR; run_ui(250); save("sensor_error");
    app_ui_next_page(); run_ui(250); save("offline");
    /* Recover from a missing DMA completion and from a failed command. */
    stall_dma = 1;
    for (unsigned i = 0; i < 100U && ST7789_GetState() != ST7789_ERROR; ++i) run_ui(1);
    assert(ST7789_GetState() == ST7789_ERROR && aborts == 1U && backlight == GPIO_PIN_RESET);
    stall_dma = 0; run_ui(2800);
    assert(ST7789_GetState() == ST7789_READY && backlight == GPIO_PIN_SET);
    while (ST7789_IsBusy()) run_ui(1);
    inject_error = 1;
    for (unsigned i = 0; i < 250U && ST7789_GetState() != ST7789_ERROR; ++i) run_ui(1);
    assert(ST7789_GetState() == ST7789_ERROR);
    run_ui(2800); assert(ST7789_GetState() == ST7789_READY);
    /* Bad windows must never start a DMA transfer. */
    unsigned before = transfers;
    assert(!ST7789_WriteRows(240, 8, BSP_LCD_GetBand()));
    assert(!ST7789_WriteRows(0, 0, BSP_LCD_GetBand()));
    assert(!ST7789_WriteRows(239, 8, BSP_LCD_GetBand()));
    assert(!ST7789_WriteRows(0, 9, BSP_LCD_GetBand()));
    assert(!ST7789_WriteRows(0, 8, 0));
    assert(transfers == before && cmd_count > 20U);
    puts("LCD integration tests passed: init, strip ownership, RGB565, alarm, timeout and recovery.");
    return 0;
}
