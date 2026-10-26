#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "main.h"
#include "rtc.h"
#include "usart.h"
#include "bsp_rtc.h"
#include "bsp_esp.h"
#include "app_time.h"
#include "app_report.h"
#include "app_json.h"
#include "bsp_gps.h"
#include "app_fall.h"
static bsp_gps_data_t gps;
static app_fall_data_t fall;
static unsigned int toggle_calls;
const bsp_gps_data_t *BSP_GPS_GetData(void) {return &gps;}
const app_fall_data_t *app_fall_get_data(void) {return &fall;}
void app_fall_toggle_alarm(void) {++toggle_calls;fall.state=fall.state==APP_FALL_ALARM?APP_FALL_WAIT_UPRIGHT:APP_FALL_ALARM;}

uint32_t mock_lse = 1U, mock_overflow;
GPIO_TypeDef mock_port;
static RTC_TypeDef registers;
RTC_HandleTypeDef hrtc = {&registers};
UART_HandleTypeDef huart3;
static uint32_t now, backup[5], set_calls, tx_calls;
static uint8_t fail_rtc;
static GPIO_PinState en;
static char transmitted[256];

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
static uint8_t reporting;
static void report_poll(void) {app_time_poll();if(reporting)app_report_poll();}
static void step(uint32_t ms) {now+=ms;counter(count()+ms/1000U);report_poll();}
static void feed(const char *text) {
 for(;*text;++text)BSP_ESP_RxByte((uint8_t)*text);
 for(unsigned int i=0;i<6;++i)report_poll();
}
static void reply(const char *text) {huart3.gState=HAL_UART_STATE_READY;feed(text);}
static void command(const char *wanted) {assert(strcmp(transmitted,wanted)==0);}
static uint8_t quoted_port;

static void finish_connection(uint8_t ok_first, uint8_t valid_time)
{
 const char *connected=quoted_port ? "+MQTTCONNECTED:0,1,\"broker.test\",\"1883\",\"\",0\r\n" :
                                    "+MQTTCONNECTED:0,1,\"broker.test\",1883,\"\",0\r\n";
 command("AT\r\n");reply("OK\r\n");command("ATE0\r\n");
 reply("OK\r\n");
 command("AT+CWMODE=1\r\n");reply("OK\r\n");
 assert(strncmp(transmitted,"AT+CWJAP=",9)==0);reply("WIFI CONNECTED\r\nWIFI GOT IP\r\nOK\r\n");
 if(strncmp(transmitted,"AT+CIPSNTPCFG=1,8,",17)==0) {
  reply("OK\r\n");step(2000);command("AT+CIPSNTPTIME?\r\n");
  if(valid_time)reply("+CIPSNTPTIME:Mon Oct 26 12:34:56 2026\r\nOK\r\n");
  else reply("ERROR\r\n");
 }
 command("AT+MQTTCLEAN=0\r\n");reply("ERROR\r\n");
 command("AT+MQTTUSERCFG=0,1,\"belt01\",\"synthetic-user\",\"synthetic-password\",0,0,\"\"\r\n");
 reply("OK\r\n");command("AT+MQTTCONNCFG=0,120,0,\"\",\"\",0,0\r\n");
 reply("OK\r\n");command("AT+MQTTCONN=0,\"broker.test\",1883,0\r\n");
 if(ok_first==2) {reply("OK\r\n");return;}
 if(ok_first) {
  reply("OK\r\n");assert(!BSP_ESP_IsOnline());
  feed(connected);
 } else {
  reply(connected);assert(!BSP_ESP_IsOnline());reply("OK\r\n");
 }
 command("AT+MQTTSUB=0,\"fallguard/belt01/cmd\",0\r\n");assert(!BSP_ESP_IsOnline());reply("OK\r\n");
 assert(BSP_ESP_IsOnline() && BSP_ESP_GetData()->state==BSP_ESP_MQTT_ONLINE);
}
static void online(uint8_t order,uint8_t valid_time)
{
 reporting=0;reset_all();memset(&gps,0,sizeof(gps));memset(&fall,0,sizeof(fall));toggle_calls=0;
 step(2000);finish_connection(order,valid_time);app_report_init();assert(app_report_get_data()->interval_seconds==5);
}
static void message(const char *topic,const char *payload)
{
 char header[192];snprintf(header,sizeof(header),"+MQTTSUBRECV:0,\"%s\",%u,",topic,(unsigned int)strlen(payload));
 feed(header);feed(payload);feed("\r\n");
}

int main(void)
{
 char buffer[128], saved[256];app_json_command_t cmd;
 const char *invalid[]={"", "{}", "{\"cmd\":\"clear\"", "{\"cmd\":\"loc\"}junk",
  "{\"cmd\":\"loc\",\"cmd\":\"clear\"}","{\"cmd\":\"loc\",}","{\"cmd\":\"loc\",\"x\":1}",
  "{\"cmd\":\"intv\",\"v\":0}","{\"cmd\":\"intv\",\"v\":3601}","{\"cmd\":\"intv\",\"v\":05}",
  "{\"cmd\":\"intv\",\"v\":5.0}","{\"cmd\":\"intv\",\"v\":-5}","{\"cmd\":\"intv\",\"v\":5e1}",
  "{\"cmd\":\"intv\",\"v\":42949672960}","{\"cmd\":\"loc\",\"v\":5}","{\"v\":5}",
  "{\"cmd\":\"intv\",\"v\":5,\"v\":6}","{\"cmd\":\"loc\\\"clear\"}"};
 for(unsigned int i=0;i<sizeof(invalid)/sizeof(invalid[0]);++i)assert(!app_json_command(invalid[i],&cmd));
 assert(app_json_command(" \r\n{ \"v\" : 5 , \"cmd\" : \"intv\" }\t",&cmd) && cmd.seconds==5);
 assert(app_json_command("{\"cmd\":\"intv\",\"v\":3600}",&cmd) && cmd.seconds==3600);
 assert(app_json_command("{\"cmd\":\"intv\",\"v\":1}",&cmd) && cmd.seconds==1);
 assert(app_json_command("{\"cmd\":\"clear\"}",&cmd) && cmd.type==APP_CMD_CLEAR);
 {app_json_snapshot_t s={4294967295U,-90000000,-180000000,1,1};
  assert(app_json_location(buffer,sizeof(buffer),&s));
  assert(strcmp(buffer,"{\"id\":\"belt01\",\"ts\":4294967295,\"lat\":-90000000,\"lon\":-180000000,\"fix\":1,\"st\":\"fall\"}")==0);
  assert(!app_json_location(buffer,8,&s));assert(app_json_event(buffer,sizeof(buffer),&s,0));
  assert(strcmp(buffer,"{\"id\":\"belt01\",\"ts\":4294967295,\"ev\":\"cancel\"}")==0);
 }
 puts("PASS strict fixed JSON commands, bounds, duplicate/trailing/unknown rejection and worst-case integer serialization");
 online(1,1);online(0,1);
 quoted_port=1;online(1,1);online(0,1);quoted_port=0;
 assert(BSP_ESP_Publish(BSP_ESP_TOPIC_LOC,"{\"id\":\"belt01\",\"fix\":0}"));
 command("AT+MQTTPUB=0,\"fallguard/belt01/loc\",\"{\\\"id\\\":\\\"belt01\\\"\\,\\\"fix\\\":0}\",0,0\r\n");
 strcpy(saved,transmitted);assert(!BSP_ESP_Publish(BSP_ESP_TOPIC_LOC,"{}"));assert(strcmp(saved,transmitted)==0);
 message(BSP_ESP_TOPIC_CMD,"\r\nOK\r\n");
 assert(BSP_ESP_GetData()->published==0 && BSP_ESP_GetData()->state==BSP_ESP_MQTT_PUBLISH);
 assert(BSP_ESP_ReadCommand(buffer,sizeof(buffer)) && strcmp(buffer,"\r\nOK\r\n")==0);
 reply("OK\r\n");assert(BSP_ESP_GetData()->published==1);
 puts("PASS quoted/numeric ports in both CONNECTED/OK orders, subscription gating, exact AT escaping and TX ownership");
 {const char *msg="+MQTTSUBRECV:0,\"fallguard/belt01/cmd\",13,{\"cmd\":\"loc\"}";
  for(;*msg;++msg){BSP_ESP_RxByte((uint8_t)*msg);app_time_poll();}
  assert(BSP_ESP_ReadCommand(buffer,sizeof(buffer)) && strcmp(buffer,"{\"cmd\":\"loc\"}")==0);
 }
 feed("+MQTTSUBRECV:0,\"fallguard/belt01/cmd\",13,{\"cmd\":\"loc\"}+MQTTSUBRECV:0,\"fallguard/belt01/cmd\",15,{\"cmd\":\"clear\"}");
 assert(BSP_ESP_ReadCommand(buffer,sizeof(buffer)) && strstr(buffer,"loc"));
 assert(BSP_ESP_ReadCommand(buffer,sizeof(buffer)) && strstr(buffer,"clear"));
 message("other/topic","{\"cmd\":\"clear\"}");assert(!BSP_ESP_ReadCommand(buffer,sizeof(buffer)));
 {char big[130];memset(big,'x',128);big[128]=0;message(BSP_ESP_TOPIC_CMD,big);}
 assert(!BSP_ESP_ReadCommand(buffer,sizeof(buffer)) && BSP_ESP_IsOnline());
 feed("+MQTTSUBRECV:0,\"fallguard/belt01/cmd\",3,");BSP_ESP_RxByte('{');BSP_ESP_RxByte(0);BSP_ESP_RxByte('}');app_time_poll();
 assert(!BSP_ESP_ReadCommand(buffer,sizeof(buffer)));
 for(unsigned int i=0;i<5;++i)message(BSP_ESP_TOPIC_CMD,"{\"cmd\":\"loc\"}");
 for(unsigned int i=0;i<4;++i)assert(BSP_ESP_ReadCommand(buffer,sizeof(buffer)));
 assert(!BSP_ESP_ReadCommand(buffer,sizeof(buffer)) && BSP_ESP_GetData()->command_drops>=4);
 puts("PASS byte-split/coalesced URCs, payload newlines/NUL/oversize, wrong-topic discard and bounded command FIFO");
 online(1,0);assert(!BSP_RTC_GetData()->valid && BSP_ESP_IsOnline());
 reporting=1;step(0);assert(strstr(transmitted,"\\\"ts\\\":0"));reply("OK\r\n");
 {uint32_t calls=tx_calls;step(4999);assert(tx_calls==calls);step(1);assert(tx_calls==calls+1);reply("OK\r\n");}
 message(BSP_ESP_TOPIC_CMD,"{\"cmd\":\"clear\"}");assert(toggle_calls==0 && fall.state==APP_FALL_IDLE);
 fall.state=APP_FALL_ALARM;step(0);assert(strstr(transmitted,"\\\"ev\\\":\\\"fall\\\""));
 message(BSP_ESP_TOPIC_CMD,"{\"cmd\":\"clear\"}");assert(toggle_calls==1 && fall.state==APP_FALL_WAIT_UPRIGHT);
 message(BSP_ESP_TOPIC_CMD,"{\"cmd\":\"clear\"}");assert(toggle_calls==1);
 reply("OK\r\n");assert(strstr(transmitted,"\\\"ev\\\":\\\"cancel\\\""));reply("OK\r\n");
 assert(strstr(transmitted,"fallguard/belt01/loc") && strstr(transmitted,"\\\"st\\\":\\\"ok\\\""));reply("OK\r\n");
 message(BSP_ESP_TOPIC_CMD,"{\"cmd\":\"intv\",\"v\":1}");assert(app_report_get_data()->interval_seconds==1);
 {uint32_t calls=tx_calls;step(999);assert(tx_calls==calls);step(1);assert(tx_calls==calls+1);reply("OK\r\n");}
 message(BSP_ESP_TOPIC_CMD,"{\"cmd\":\"intv\",\"v\":0}");assert(app_report_get_data()->bad_commands==1);
 message(BSP_ESP_TOPIC_CMD,"{\"cmd\":\"loc\"}");assert(strstr(transmitted,"AT+MQTTPUB=0,\"fallguard/belt01/loc\""));reply("OK\r\n");
 puts("PASS SNTP failure still permits MQTT, ts=0, periodic boundaries, immediate loc, idempotent remote clear and fall/cancel ordering");
 feed("+MQTTDISCONNECTED:0\r\n");assert(!BSP_ESP_IsOnline());fall.state=APP_FALL_ALARM;step(0);
 fall.state=APP_FALL_WAIT_UPRIGHT;step(0);fall.state=APP_FALL_ALARM;step(0);
 reporting=0;step(30000);assert(en==GPIO_PIN_RESET);step(100);step(2000);finish_connection(0,0);
 reporting=1;step(0);assert(strstr(transmitted,"\\\"st\\\":\\\"fall\\\"") && !strstr(transmitted,"\\\"ev\\\""));reply("OK\r\n");
 assert(app_report_get_data()->interval_seconds==1);
 reporting=0;feed("+MQTTSUBRECV:0,\"fallguard/belt01/cmd\",20,{\"cmd\":");step(2000);
 assert(!BSP_ESP_IsOnline() && BSP_ESP_GetData()->state==BSP_ESP_BACKOFF);
 online(1,1);feed("+MQTTSUBRECV:0,\"fallguard/belt01/cmd\",20,{");BSP_ESP_RxError(8);step(0);
 assert(!BSP_ESP_IsOnline() && !BSP_ESP_ReadCommand(buffer,sizeof(buffer)));
 puts("PASS disconnect reconnect/resubscribe, current-state-only recovery, interval retention, incomplete-payload timeout and UART-loss fail closed");
 online(2,1);assert(!BSP_ESP_IsOnline());
 feed("+MQTTCONNECTED:0,1,\"other.test\",\"1883\",\"\",0\r\n");
 feed("+MQTTCONNECTED:0,1,\"broker.test\",\"1884\",\"\",0\r\n");
 feed("+MQTTCONNECTED:1,1,\"broker.test\",\"1883\",\"\",0\r\n");
 feed("+MQTTCONNECTED:0,1,\"broker.test\",\"1883\",\"\",0junk\r\n");
 step(29999);assert(!BSP_ESP_IsOnline());step(1);
 assert(BSP_ESP_GetData()->state==BSP_ESP_BACKOFF && BSP_ESP_GetData()->timeouts==1);
 online(1,1);assert(BSP_ESP_Publish(BSP_ESP_TOPIC_LOC,"{}"));reply("ERROR\r\n");
 assert(!BSP_ESP_IsOnline() && BSP_ESP_GetData()->published==0 && BSP_ESP_GetData()->publish_failures==1);
 puts("PASS CONNECT OK without matching URC cannot go online, and publish ERROR is not counted as success");
 online(1,0);reporting=1;step(0);reply("OK\r\n");
 message(BSP_ESP_TOPIC_CMD,"{\"cmd\":\"intv\",\"v\":3600}");
 assert(app_report_get_data()->interval_seconds==3600);
 gps.has_position=gps.fix=1;gps.latitude_udeg=22615755;gps.longitude_udeg=114141317;
 fall.state=APP_FALL_ALARM;
 feed("+MQTTSUBRECV:0,\"fallguard/belt01/cmd\",15,{\"cmd\":\"clear\"}");
 assert(toggle_calls==1 && fall.state==APP_FALL_WAIT_UPRIGHT);
 assert(strstr(transmitted,"\\\"ev\\\":\\\"fall\\\"") && strstr(transmitted,"22615755"));
 reply("OK\r\n");assert(strstr(transmitted,"\\\"ev\\\":\\\"cancel\\\""));
 reply("OK\r\n");assert(strstr(transmitted,"fallguard/belt01/loc") && strstr(transmitted,"\\\"st\\\":\\\"ok\\\""));
 reply("OK\r\n");
 gps.fix=0;gps.latitude_udeg=-90000000;gps.longitude_udeg=-180000000;
 message(BSP_ESP_TOPIC_CMD,"{\"cmd\":\"loc\"}");
 assert(strstr(transmitted,"-90000000") && strstr(transmitted,"\\\"fix\\\":0") && !strstr(transmitted,"sat"));
 reply("OK\r\n");
 puts("PASS simultaneous new alarm/remote clear preserves both events, immediate status ignores long period, and invalid fix retains integer coordinates without sat");
 return 0;
}
