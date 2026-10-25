#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "main.h"
#include "tim.h"
#include "bsp_jq8900.h"
#include "app_voice.h"

typedef struct {uint64_t at;GPIO_PinState level;} edge_t;
GPIO_TypeDef mock_port;
TIM_HandleTypeDef htim3;
static uint64_t micros, next_irq, next_poll;
static uint64_t starts[512];
static edge_t edges[10000];
static unsigned int n_edges, n_starts;
static uint8_t timer_running, fail_start, use_voice, desired_alarm, busy_level;
static GPIO_PinState sda=GPIO_PIN_SET;
uint32_t HAL_GetTick(void) {return (uint32_t)(micros/1000U);}
HAL_StatusTypeDef HAL_TIM_Base_Start_IT(TIM_HandleTypeDef *timer) {
 assert(timer==&htim3);
 if(fail_start)return HAL_ERROR;
 assert(!timer_running && n_starts<512);
 timer_running=1;starts[n_starts++]=micros;next_irq=micros+timer->arr+1U;return HAL_OK;
}
HAL_StatusTypeDef HAL_TIM_Base_Stop_IT(TIM_HandleTypeDef *timer) {
 assert(timer==&htim3);timer_running=0;return HAL_OK;
}
void HAL_GPIO_WritePin(GPIO_TypeDef *port,uint16_t pin,GPIO_PinState level) {
 assert(port==JQ_SDA_GPIO_Port && pin==JQ_SDA_Pin);
 if(level!=sda){assert(n_edges<10000);edges[n_edges].at=micros;edges[n_edges++].level=level;}
 sda=level;
}
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port,uint16_t pin) {
 assert(port==JQ_BUSY_GPIO_Port && pin==JQ_BUSY_Pin);return busy_level?GPIO_PIN_SET:GPIO_PIN_RESET;
}
#include "../../BSP/bsp_jq8900.c"
#include "../../App/app_voice.c"

static void run_us(uint64_t duration) {
 uint64_t end=micros+duration;
 while(micros<end) {
  uint64_t at=next_poll;
  if(timer_running && next_irq<at)at=next_irq;
  if(at>end){micros=end;break;}
  micros=at;
  if(timer_running && micros==next_irq) {
   BSP_JQ8900_TimerIRQ();
   if(timer_running)next_irq=micros+htim3.arr+1U;
  }
  if(micros==next_poll) {
   if(use_voice)app_voice_poll(desired_alarm);else BSP_JQ8900_Poll();
   next_poll+=100U;
  }
 }
}
static void reset(uint8_t voice,uint64_t start) {
 timer_running=fail_start=busy_level=desired_alarm=0;sda=GPIO_PIN_SET;
 micros=start;next_poll=micros;n_edges=n_starts=0;use_voice=voice;
 if(voice)app_voice_init();else BSP_JQ8900_Init();
}
static void ready(void) {reset(0,0);run_us(1001000U);assert(data.ready && !BSP_JQ8900_IsSending());}
static void drain(void) {
 unsigned int i;
 for(i=0;i<10000 && BSP_JQ8900_IsSending();++i)run_us(100U);
 assert(!BSP_JQ8900_IsSending() && !data.error && sda==GPIO_PIN_SET);
}
static unsigned int decode(uint8_t *out) {
 unsigned int n,b;
 assert(n_edges%18U==0U);
 for(n=0;n<n_edges/18U;++n) {
  unsigned int base=n*18U;uint8_t byte=0;
  assert(edges[base].level==GPIO_PIN_RESET && edges[base+1].level==GPIO_PIN_SET);
  assert(edges[base].at-starts[n]==1000U);
  assert(edges[base+1].at-edges[base].at==4000U);
  if(n)assert(edges[base].at-edges[base-1].at>=12000U);
  for(b=0;b<8;++b) {
   unsigned int hi=base+1U+2U*b,lo=hi+1U;
   uint64_t high=edges[lo].at-edges[hi].at,low=edges[lo+1].at-edges[lo].at;
   assert(edges[hi].level==GPIO_PIN_SET && edges[lo].level==GPIO_PIN_RESET);
   assert((high==500U && low==1500U)||(high==1500U && low==500U));
   if(high>low)byte|=(uint8_t)(1U<<b);
  }
  assert(edges[base+17].at-starts[n]==21000U);out[n]=byte;
 }
 return n_edges/18U;
}
static void expect(const uint8_t *wanted,unsigned int count) {
 uint8_t actual[512];unsigned int n=decode(actual);
 assert(n==count && !memcmp(actual,wanted,count));
}
static void clear_trace(void) {assert(!BSP_JQ8900_IsSending());n_edges=n_starts=0;}

int main(void) {
 uint8_t byte,actual[512];unsigned int n,i;
 static const uint8_t play2[]={0x0A,0x02,0x0B},play15[]={0x0A,0x01,0x05,0x0B};
 static const uint8_t vol20[]={0x0A,0x02,0x00,0x0C},maxtrack[]={0x0A,9,9,9,9,9,0x0B};
 ready();byte=0x89;assert(queue_command(&byte,1));drain();expect(&byte,1);
 puts("PASS manual 0x89 LSB waveform: 1ms high, 4ms guide, exact 500/1500us bits and idle high");
 ready();for(i=0;i<256;++i){byte=(uint8_t)i;assert(queue_command(&byte,1));drain();}
 n=decode(actual);assert(n==256);for(i=0;i<n;++i)assert(actual[i]==i);
 puts("PASS all 256 byte values and inter-byte high gap greater than 10ms");
 ready();assert(BSP_JQ8900_Play(2));assert(!BSP_JQ8900_Play(15));drain();expect(play2,3);
 clear_trace();assert(BSP_JQ8900_Play(15));drain();expect(play15,4);
 clear_trace();assert(BSP_JQ8900_SetVolume(20));drain();expect(vol20,4);
 clear_trace();assert(BSP_JQ8900_Play(99999));drain();expect(maxtrack,7);
 assert(!BSP_JQ8900_Play(0) && !BSP_JQ8900_Play(100000) && !BSP_JQ8900_SetVolume(31));
 puts("PASS PDF track 2/15 and volume 20 command bytes, 5-digit bound and busy rejection");
 ready();assert(BSP_JQ8900_Play(15));
 while(!(tx_active && command_index==1 && tx_bit==3))run_us(100U);
 BSP_JQ8900_Stop();drain();{const uint8_t wanted[]={0x0A,0x01,0x13};expect(wanted,3);}
 clear_trace();assert(BSP_JQ8900_Play(2));drain();expect(play2,3);
 ready();assert(BSP_JQ8900_Play(15));
 while(!(tx_active && command_index==3 && tx_bit==3))run_us(100U);
 BSP_JQ8900_Stop();drain();{const uint8_t wanted[]={0x0A,0x01,0x05,0x0B,0x13};expect(wanted,5);}
 puts("PASS urgent stop finishes active byte, drops unsent selection digits and remains reusable");
 reset(0,((uint64_t)0xFFFFFFFFU-500U)*1000U);run_us(1001000U);
 assert(BSP_JQ8900_SetVolume(20));drain();expect(vol20,4);
 ready();fail_start=1;assert(BSP_JQ8900_Play(2));run_us(100U);
 assert(data.error && !tx_active && sda==GPIO_PIN_SET && !timer_running);
 puts("PASS boot/gap tick rollover and timer-start failure leaves idle high");
 reset(1,0);run_us(1500000U);assert(app_voice_get_state()==APP_VOICE_IDLE);
 {const uint8_t wanted[]={0x1A,0x13,0x0A,0x02,0x00,0x0C};expect(wanted,6);}
 clear_trace();desired_alarm=1;run_us(200000U);
 {const uint8_t wanted[]={0x0A,0x01,0x0B};expect(wanted,3);}
 assert(app_voice_get_state()==APP_VOICE_WAIT_BUSY);
 run_us(300000U);assert(n_edges==54);
 busy_level=1;run_us(1000U);assert(app_voice_get_state()==APP_VOICE_PLAYING);
 run_us(300000U);assert(n_edges==54);
 busy_level=0;run_us(200000U);
 {const uint8_t wanted[]={0x0A,0x01,0x0B,0x0A,0x01,0x0B};expect(wanted,6);}
 busy_level=1;run_us(1000U);desired_alarm=0;run_us(100000U);
 n=decode(actual);assert(actual[n-1]==0x13 && app_voice_get_state()==APP_VOICE_IDLE);
 i=n_edges;run_us(300000U);assert(n_edges==i);
 puts("PASS actual voice controller: startup flash/stop/volume, alarm selection, BUSY high-to-low repeat and cancel stop");
 reset(1,0);desired_alarm=1;run_us(100000U);desired_alarm=0;run_us(1500000U);
 {const uint8_t wanted[]={0x1A,0x13,0x0A,0x02,0x00,0x0C};expect(wanted,6);}
 assert(app_voice_get_state()==APP_VOICE_IDLE);
 puts("PASS alarm canceled before module startup does not cause late playback");
 return 0;
}
