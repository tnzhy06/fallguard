#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "main.h"
#include "rtc.h"
#include "usart.h"
#include "bsp_rtc.h"
#include "bsp_esp.h"
#include "app_time.h"

uint32_t mock_lse = 1U, mock_overflow;
GPIO_TypeDef mock_port;
static RTC_TypeDef registers;
RTC_HandleTypeDef hrtc = {&registers};
UART_HandleTypeDef huart3;
static uint32_t now, backup[5], set_calls, tx_calls;
static uint8_t fail_rtc;
static GPIO_PinState en;
static char transmitted[224];

uint32_t HAL_GetTick(void) {return now;}
static void counter(uint32_t value) {registers.CNTH=value>>16;registers.CNTL=value&0xffffU;}
static uint32_t count(void) {return (registers.CNTH<<16)|registers.CNTL;}
void HAL_GPIO_WritePin(GPIO_TypeDef *port,uint16_t pin,GPIO_PinState value) {
 assert(port==ESP_EN_GPIO_Port && pin==ESP_EN_Pin);en=value;
}
HAL_StatusTypeDef HAL_UART_Transmit_IT(UART_HandleTypeDef *uart,uint8_t *bytes,uint16_t length) {
 assert(uart==&huart3 && length<sizeof(transmitted));
 if(uart->gState!=HAL_UART_STATE_READY)return HAL_BUSY;
 memcpy(transmitted,bytes,length);transmitted[length]=0;++tx_calls;
 uart->gState=HAL_UART_STATE_BUSY_TX;return HAL_OK;
}
HAL_StatusTypeDef HAL_UART_AbortTransmit(UART_HandleTypeDef *uart) {
 uart->gState=HAL_UART_STATE_READY;return HAL_OK;
}
HAL_StatusTypeDef HAL_RTC_SetTime(RTC_HandleTypeDef *rtc,RTC_TimeTypeDef *time,uint32_t format) {
 assert(rtc==&hrtc && format==RTC_FORMAT_BIN);++set_calls;
 if(fail_rtc)return HAL_ERROR;
 counter(time->Hours*3600U+time->Minutes*60U+time->Seconds);mock_overflow=0;return HAL_OK;
}
uint32_t HAL_RTCEx_BKUPRead(RTC_HandleTypeDef *rtc,uint32_t reg) {(void)rtc;return backup[reg];}
void HAL_RTCEx_BKUPWrite(RTC_HandleTypeDef *rtc,uint32_t reg,uint32_t value) {
 (void)rtc;assert(reg<5 && value<=65535U);backup[reg]=value;
}
static void reset_all(void) {
 memset(backup,0,sizeof(backup));counter(0);now=0;mock_lse=1;mock_overflow=0;
 set_calls=tx_calls=fail_rtc=0;huart3.gState=HAL_UART_STATE_READY;transmitted[0]=0;app_time_init();
}
static void step(uint32_t ms) {now+=ms;counter(count()+ms/1000U);app_time_poll();}
static void feed(const char *text) {
 for(;*text;++text)BSP_ESP_RxByte((uint8_t)*text);
 for(unsigned int i=0;i<6;++i)app_time_poll();
}
static void reply(const char *text) {huart3.gState=HAL_UART_STATE_READY;feed(text);}
static void command(const char *wanted) {assert(strcmp(transmitted,wanted)==0);}
static void connect_to_sntp(void) {
 step(2000);command("AT\r\n");reply("AT\r\n\r\nOK\r\n");command("ATE0\r\n");
 reply("OK\r\n");
#ifndef TEST_NO_WIFI
 command("AT+CWMODE=1\r\n");reply("OK\r\n");
 command("AT+CWJAP=\"demo\\,\\\"ssid\\\\\",\"synthetic-test-password\"\r\n");
 reply("WIFI CONNECTED\r\nWIFI GOT IP\r\nOK\r\n");
 command("AT+CIPSNTPCFG=1,8,\"ntp.aliyun.com\",\"ntp.sjtu.edu.cn\",\"cn.ntp.org.cn\"\r\n");reply("OK\r\n");
 assert(!BSP_RTC_GetData()->valid && set_calls==0);
 step(2000);command("AT+CIPSNTPTIME?\r\n");
#endif
}

int main(void) {
 bsp_rtc_datetime_t value={2024,2,29,23,59,59};
 reset_all();assert(!BSP_RTC_HasBackup() && !BSP_RTC_GetData()->valid);
 assert(BSP_RTC_ToUnixUtc(&value)==1709222399U);
 assert(BSP_RTC_Set(&value));counter(count()+1);BSP_RTC_Poll();
 assert(BSP_RTC_GetData()->local.month==3 && BSP_RTC_GetData()->local.day==1);
 assert(BSP_RTC_GetData()->local.hour==0 && BSP_RTC_GetData()->unix_utc==1709222400U);
 counter(count()+86400U*40U);BSP_RTC_Init();
 assert(BSP_RTC_GetData()->valid && BSP_RTC_GetData()->restored);
 assert(BSP_RTC_GetData()->local.month==4 && BSP_RTC_GetData()->local.day==10);
 value.year=2023;assert(!BSP_RTC_Set(&value));value.year=2024;value.second=60;assert(!BSP_RTC_Set(&value));
 value.year=2099;value.month=12;value.day=31;value.second=59;
 assert(BSP_RTC_Set(&value) && BSP_RTC_HasBackup());
 assert(BSP_RTC_GetData()->unix_utc==4102415999U);
 counter(count()+1);BSP_RTC_Poll();assert(!BSP_RTC_GetData()->valid);
 value.year=2000;value.month=1;value.day=1;value.hour=value.minute=value.second=0;
 assert(BSP_RTC_Set(&value) && BSP_RTC_GetData()->unix_utc==946656000U);
 backup[4]^=1;BSP_RTC_Init();assert(!BSP_RTC_GetData()->valid);
 assert(BSP_RTC_Set(&value));mock_overflow=1;BSP_RTC_Poll();assert(!BSP_RTC_GetData()->valid);
 mock_overflow=0;fail_rtc=1;assert(!BSP_RTC_Set(&value) && !BSP_RTC_HasBackup());
 puts("PASS RTC UTC+8 conversion, leap/month/year rollover, 40-day reset restore, invalid dates, range, backup corruption and HAL failure");
 reset_all();connect_to_sntp();
#ifdef TEST_NO_WIFI
 assert(BSP_ESP_GetData()->state==BSP_ESP_NO_CONFIG && tx_calls==2);
 puts("PASS empty credentials only probe AT/ATE0, then wait without attempting WiFi");
#else
 reply("+CIPSNTPTIME:Thu Jan  1 00:00:00 1970\r\nOK\r\n");
 assert(!BSP_RTC_GetData()->valid);step(2000);
 reply("+CIPSNTPTIME:Mon Feb 30 12:34:56 2026\r\nOK\r\n");
 assert(!BSP_RTC_GetData()->valid);step(2000);
 reply("+CIPSNTPTIME:Mon Oct 26 12:34:56 2026\r\n");
 assert(set_calls==0);feed("OK\r\n");
 assert(set_calls==1 && BSP_RTC_GetData()->valid);
 assert(BSP_RTC_GetData()->local.hour==12 && BSP_RTC_GetData()->local.day==26);
 assert(BSP_ESP_GetData()->time_sequence==1 && BSP_ESP_GetData()->state==BSP_ESP_READY);
 {uint32_t utc=BSP_RTC_GetData()->unix_utc;feed("WIFI DISCONNECT\r\n");step(5000);
  assert(!BSP_ESP_GetData()->wifi_connected && BSP_RTC_GetData()->unix_utc==utc+5);}
 puts("PASS actual AT/WiFi/SNTP sequence, escaping, 1970/invalid-date rejection, final-OK gating and independent RTC holdover");
 reset_all();connect_to_sntp();
 reply("+CIPSNTPTIME:Mon Oct 26 12:34:56 2026\r\nERROR\r\n");
 assert(!BSP_RTC_GetData()->valid && BSP_ESP_GetData()->state==BSP_ESP_READY);
 reset_all();connect_to_sntp();
 for(unsigned int i=0;i<20 && BSP_ESP_GetData()->state==BSP_ESP_SNTP_QUERY;++i) {
  reply("+CIPSNTPTIME:Thu Jan  1 00:00:00 1970\r\nOK\r\n");step(2000);
 }
 assert(BSP_ESP_GetData()->state==BSP_ESP_READY && !BSP_RTC_GetData()->valid);
 {uint32_t n=tx_calls;step(59000);assert(tx_calls==n);step(1000);assert(tx_calls==n+1);}
 puts("PASS SNTP rejection permits further networking and bounded acquisition/retry without false calibration");
#endif
 reset_all();step(2000);step(2000);assert(BSP_ESP_GetData()->timeouts==1);
 step(30000);assert(en==GPIO_PIN_RESET);step(99);assert(en==GPIO_PIN_RESET);
 step(1);assert(en==GPIO_PIN_SET);step(2000);command("AT\r\n");
 BSP_ESP_RxByte('O');BSP_ESP_RxError(8);feed("K\r\n");
 assert(BSP_ESP_GetData()->state==BSP_ESP_BACKOFF && !BSP_RTC_GetData()->valid);
 reset_all();step(2000);for(unsigned int i=0;i<1100;++i)BSP_ESP_RxByte('X');app_time_poll();
 assert(BSP_ESP_GetData()->dropped_bytes>0 && BSP_ESP_GetData()->state==BSP_ESP_BACKOFF);
 reset_all();step(2000);for(unsigned int i=0;i<200;++i)BSP_ESP_RxByte('X');app_time_poll();app_time_poll();app_time_poll();
 assert(BSP_ESP_GetData()->bad_lines==1 && BSP_ESP_GetData()->state==BSP_ESP_BACKOFF);
 reset_all();BSP_ESP_RxError(4);feed("boot noise");
 assert(BSP_ESP_GetData()->state==BSP_ESP_BOOT);step(2000);command("AT\r\n");
 reset_all();now=0xffffff00U;BSP_ESP_Init();step(1999);assert(tx_calls==0);step(1);assert(tx_calls==1);
 puts("PASS timeout recovery, EN 100ms, UART loss/ring/line overflow resynchronization and tick wrap");
 return 0;
}
