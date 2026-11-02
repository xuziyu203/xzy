#include "joystick_ads1115.h"
#include "bsp.h"
#include "soc.h"
#include <string.h>
/* Single owner of I2C0. No BSP blocking functions, ISR or DDR access.
 * All deadlines use CLINT100; timestamp issue precedes the config transaction. */
#define JT_MS(n) ((u64)(BSP_CLINT_HZ/1000u)*(n))
#define JT_US(n) ((u64)(BSP_CLINT_HZ/1000000u)*(n))
#define JT_FRESH ((u64)BSP_CLINT_HZ/25u) /* CMDJ: player_input.c joystick freshness window */
#define JB SYSTEM_I2C_0_IO_CTRL
#define JG 0xf8015000u
enum { J_IDLE,J_CONFIG,J_WAIT,J_READY,J_READ,J_STOP,J_DROP,J_BACKOFF,J_LATCH,J_INIT,J_BUSREC }; /* GHDA: J_BUSREC appended */
enum { T_CLEAR,T_IDLE,T_START,T_START_WAIT,T_SEND,T_ACK,T_VALUE,T_STOP,T_STOP_WAIT,T_DONE };
enum { D_NONE,D_TRANSACTION_TIMEOUT,D_BYTE_TIMEOUT,D_IDLE_BUSY,D_IDLE_LINES,
 D_START_FLAGS,D_START_NOT_BUSY,D_TX_MISSING_VALID,D_TX_NACK,D_RX_MISSING_VALID,
 D_STOP_FLAGS,D_RESERVED_STOP,D_STOP_LINES,D_READY_TIMEOUT,D_CONFIG_MISMATCH,
 D_PAIR_TIMEOUT,D_SERVICE_GAP,D_TIME_BACKWARD,D_RECOVERY_LINES,D_BUS_RECOVER }; /* GHDA: D_BUS_RECOVER appended (19 < 20 classes) */
typedef struct {u64 issue[2],ready[2],done[2],read[2];int16_t raw[2];} joystick_pair_t;
static joystick_status_t js;
static joystick_diag_t jd;
/* GHDA: a latched driver re-initialises itself once per JOY_AUTO_PERIOD (the KEY3 joystick_enable path) instead of waiting for KEY3.
 * An attempt is bounded by the existing per-call budget and timeouts (1 ms byte, 3 ms transaction / ready, 3 probes with back-off,
 * 2 s episode) and by JOY_AUTO_CAP: ok = 2 new valid pairs; fail = latched again. Input stays invalid until re-armed (2 good pairs +
 * 100 ms neutral), exactly as after any fault. GHDB1 limits consecutive automatic
 * attempts to three; two fresh pairs clear the limit, KEY3/RETRY renew it explicitly. */
#define JOY_AUTO_PERIOD JT_MS(1000)
#define JOY_AUTO_CAP JT_MS(2500)
static joystick_recovery_t jrec;
static struct {
 u64 episode;
 u32 due,deadline,byte_since,transaction_since,neutral;
 u32 epoch;
 joystick_pair_t sample;
 int16_t center[2];
 uint16_t result;
 uint8_t state,ts,pos,kind,axis,init,tries,episode_on,good,invert,mask,lock,mmio,steps;
 uint8_t pump_reason,csr_valid,permission_stage;
 u32 service_gap,service_gap_valid,csr_value[5],csr_point[5],csr_when[5];
 u32 permission_data,permission_we,permission_data_write,permission_we_write;
 u64 observed_at;
 u64 prev_issue; /* CMDJ: issue[0] of the previous published pair, 0 after invalid()/epoch change */
 u32 pend_ref,pend_cur,pend_sum; /* CMDJ: transaction busy time seen by the driver: command issue time, its max pending age, sum */
} j;
static u32 sat_elapsed(u64 now,u64 before){return now<before?UINT32_MAX:now-before>UINT32_MAX?UINT32_MAX:(u32)(now-before);}
static __attribute__((noinline)) u32 jr(unsigned r){
 u32 v;unsigned i;
 j.mmio++;v=read_u32(JB+r);
 i=r==0x40?0:r==0x44?1:r==4?2:r==8?3:r==12?4:5;
 if(i<5){j.csr_valid|=1u<<i;j.csr_value[i]=v;
  j.csr_point[i]=(u32)j.state|((u32)j.ts<<8)|((u32)j.pos<<16)|((u32)j.kind<<24)|((u32)j.axis<<28);
  j.csr_when[i]=(u32)j.observed_at;}
 return v;
}
static __attribute__((noinline)) void jw(unsigned r,u32 v){j.mmio++;write_u32(v,JB+r);}
static void permission(int on){
 u32 a=read_u32(JG+4),b=read_u32(JG+8),aw=(a&~1u)|(on!=0),bw=(b&~1u)|1u;
 j.permission_stage=on?2:1;j.permission_data=a;j.permission_we=b;
 j.permission_data_write=aw;j.permission_we_write=bw;
 write_u32(aw,JG+4);write_u32(bw,JG+8);
}
static void invalid(u64 now){player_input_service(now,1);player_input_local_fault(now);js.valid=js.armed=j.mask=j.good=0;j.neutral=0;j.prev_issue=0;}
void joystick_init(void){memset(&j,0,sizeof(j));memset(&js,0,sizeof(js));memset(&jd,0,sizeof(jd));memset(&jrec,0,sizeof(jrec));jd.abi=1;joystick_enable(clint_getTime(BSP_CLINT),1);}
const joystick_status_t *joystick_status(void){return &js;}
const joystick_diag_t *joystick_diag(void){return &jd;}
const joystick_recovery_t *joystick_recovery(void){return &jrec;}
u32 joystick_take_gap_window(void){u32 g=jrec.gap_win_max;jrec.gap_win_max=0;return g;}
void joystick_retry(u64 now){
 jrec.consecutive=jrec.exhausted=0;
 jrec.latch_seen=jrec.active=0;
 if(jd.retry_epoch!=UINT32_MAX)jd.retry_epoch++;
 memset(&jd.first[1],0,sizeof(jd.first[1]));
 joystick_enable(now,1);
}
void joystick_restart_recover(u64 now){
 if(!js.enabled || j.state!=J_LATCH)return;
 if(js.restart_recoveries!=UINT32_MAX)js.restart_recoveries++;
 jrec.consecutive=jrec.exhausted=0;
 jrec.latch_seen=0;jrec.active=0; /* GHDA: a KEY3 recovery is not an automatic attempt */
 joystick_enable(now,1);
}
void joystick_enable(u64 now,int enable){
 permission(0);invalid(now);js.enabled=enable!=0;js.latched=js.error=0;j.episode_on=j.tries=0;js.age_max=js.cover_max=0;
 j.state=J_INIT;j.init=0;j.due=now+JT_MS(1);j.epoch=player_input_local_epoch();
}
int joystick_center(u64 now,const u32 v[3]){
 if(v[0]<1024 || v[0]>31743 || v[1]<1024 || v[1]>31743 || v[2]>3)return 0;
 j.center[0]=v[0];j.center[1]=v[1];j.invert=v[2];js.calibrated=1;invalid(now);return 1;
}
static void capture(joystick_diag_fault_t *f,u64 now,unsigned error,unsigned reason){
 unsigned i;
 if(!f->valid){
  f->epoch=jd.retry_epoch;f->error=error;f->reason=reason;
  f->now_lo=(u32)now;f->now_hi=(u32)(now>>32);
  f->state=j.state;f->ts=j.ts;f->pos=j.pos;f->kind=j.kind;f->axis=j.axis;
  f->service_gap=j.service_gap;
  f->timing_valid=(j.service_gap_valid?1u:0u)|
   ((j.state==J_CONFIG || j.state==J_READY || j.state==J_READ)?6u:0u)|
   ((j.state>=J_CONFIG && j.state<=J_READ)?8u:0u);
  f->transaction_elapsed=(u32)((u32)now-j.transaction_since);
  f->byte_elapsed=(u32)((u32)now-j.byte_since);
  f->pair_elapsed=sat_elapsed(now,j.sample.issue[0]);
  f->csr_valid=j.csr_valid;
  for(i=0;i<5;i++){f->csr_value[i]=j.csr_value[i];f->csr_point[i]=j.csr_point[i];
   f->csr_age[i]=(j.csr_valid&(1u<<i))?(u32)((u32)now-j.csr_when[i]):UINT32_MAX;}
  f->permission_stage=j.permission_stage;f->permission_data=j.permission_data;
  f->permission_we=j.permission_we;f->permission_data_write=j.permission_data_write;
  f->permission_we_write=j.permission_we_write;f->valid=1;
 }
}
static void fault(u64 now,unsigned error,unsigned reason){
 if(reason<JOY_DIAG_CLASS_COUNT && jd.count[reason]!=UINT32_MAX)jd.count[reason]++;
 capture(&jd.first[0],now,error,reason);
 if(jd.retry_epoch)capture(&jd.first[1],now,error,reason);
 invalid(now);js.error=error;if(js.faults!=0xffffffffu)js.faults++;
 if(!j.episode_on){j.episode_on=1;j.episode=now;j.tries=0;}
 j.state=J_STOP;j.deadline=now+JT_MS(1);jw(0x40,0x20);
}
static __attribute__((noinline)) void transaction(u64 now,unsigned kind){j.kind=kind;j.pos=0;j.ts=T_CLEAR;j.transaction_since=j.byte_since=now;j.result=0;j.csr_valid=0;j.pend_ref=(u32)now;j.pend_cur=j.pend_sum=0;}
/* CMDJ: called only when the controller still reports the step pending (after its status read). 1 ms: the current command.
 * 3 ms: controller-observed busy time of the transaction = sum over its commands of the longest pending age seen (a lower
 * bound of the real busy time). A late service that finds a step complete adds nothing and proceeds. */
static int joy_pending(u64 now){
 u32 age=(u32)now-j.byte_since;
 if(age>=JT_MS(1)){j.pump_reason=D_BYTE_TIMEOUT;return -1;}
 if(j.pend_ref!=j.byte_since){j.pend_sum+=j.pend_cur;j.pend_cur=0;j.pend_ref=j.byte_since;}
 if(age>j.pend_cur)j.pend_cur=age;
 if(j.pend_sum+j.pend_cur>=JT_MS(3)){j.pump_reason=D_TRANSACTION_TIMEOUT;return -1;}
 return 0;
}
/* Returns -1 on error, 0 pending, 1 complete, 2 progressed (CMDJ); one sub-step has <=4 MMIO. */
static int pump(u64 now){
 u32 v;unsigned rx=j.kind && j.pos>=3;
 switch(j.ts){
 case T_CLEAR:(void)jr(8);(void)jr(12);jw(8,0);jw(12,0x200);j.ts=T_IDLE;break;
 case T_IDLE:if(jr(0x40)&1u){j.pump_reason=D_IDLE_BUSY;return -1;}if((jr(0x44)&6u)!=6u){j.pump_reason=D_IDLE_LINES;return -1;}j.ts=T_START;break;
 case T_START:jw(0x40,0xe10);j.byte_since=now;j.ts=T_START_WAIT;break;
 case T_START_WAIT:v=jr(0x40);if(v&0xe00){j.pump_reason=D_START_FLAGS;return -1;}
  if(v&0x10)return joy_pending(now);
  if(!(v&1u)){j.pump_reason=D_START_NOT_BUSY;return -1;}j.ts=T_SEND;break;
 case T_SEND:
  if(rx){jw(8,0x200);jw(12,0);}
  v=0xff;
  if(j.pos==0)v=0x90;else if(j.pos==1)v=j.kind==2?0:1;
  else if(j.pos==2)v=j.kind?0x91:(j.axis?0xd3:0xc3);else if(!j.kind)v=0xe3;
  jw(0,v|0xb00);jw(4,rx && j.pos==3?0x300:0x301);j.byte_since=now;j.ts=T_ACK;break;
 case T_ACK:v=jr(4);if(v&0x100)return joy_pending(now);j.ts=T_VALUE;break;
 case T_VALUE:
  v=jr(rx?8:12);if(!(v&0x100)){j.pump_reason=rx?D_RX_MISSING_VALID:D_TX_MISSING_VALID;return -1;}
  if(!rx && (v&0xff)){j.pump_reason=D_TX_NACK;return -1;}
  if(rx)j.result=(j.result<<8)|(v&255u);
  j.pos++;
  if(j.pos==(j.kind?5:4))j.ts=T_STOP;
  else if(j.kind && j.pos==2)j.ts=T_START;else j.ts=T_SEND;break;
 case T_STOP:jw(0x40,0x20);j.byte_since=now;j.ts=T_STOP_WAIT;break;
 case T_STOP_WAIT:v=jr(0x40);if(v&0xe00){j.pump_reason=D_STOP_FLAGS;return -1;}
  if(v&0x21)return joy_pending(now);
  if((jr(0x44)&6u)!=6u){j.pump_reason=D_STOP_LINES;return -1;}j.ts=T_DONE;break;
 case T_DONE:return 1;
 }return 2; /* CMDJ: sub-step done, controller not pending */
}
static void publish(u64 now){
 unsigned a;u32 mask=0;int neutral=1;u32 age=sat_elapsed(now,j.sample.issue[0]);
 memcpy(js.issue,j.sample.issue,64);memcpy(js.raw,j.sample.raw,4);js.valid=1;
 js.sequence++;js.pairs++;js.epoch=j.epoch;js.error=0;
 /* CMDJ: a complete pair is classified by its age (no mid-pair wall-clock deadline). */
 if(age>js.age_max)js.age_max=age;
 if(j.prev_issue){u32 cover=sat_elapsed(now,j.prev_issue);if(cover>js.cover_max)js.cover_max=cover;}
 j.prev_issue=j.sample.issue[0];
 if(age>=JT_MS(10) && js.late!=UINT32_MAX)js.late++;
 if(age>=JT_FRESH){if(js.stale!=UINT32_MAX)js.stale++;return;} /* too old for player_input: never drives the player */
 if(j.good<2)j.good++;if(j.good==2)j.episode_on=j.tries=0;
 if(j.good==2 && jrec.active){u64 d=now-jrec.t0;u32 t=d>UINT32_MAX?UINT32_MAX:(u32)d;jrec.active=0;jrec.ok++;jrec.consecutive=jrec.exhausted=0;jrec.last_ticks=t;if(t>jrec.max_ticks)jrec.max_ticks=t;} /* GHDA */
 jrec.last_fresh=now; /* GHDA */
 if(!js.calibrated)return;
 for(a=0;a<2;a++){
  int x=(int)j.sample.raw[a]-j.center[a];unsigned old=(j.mask>>(a*2))&3u;int threshold=old?768:1024;
  if(j.invert&(1u<<a))x=-x;
  if(x>512 || x<-512)neutral=0;
  if(x>threshold)mask|=2u<<(a*2);else if(x<-threshold)mask|=1u<<(a*2);
 }
 if(!js.armed){if(neutral){if(!j.neutral)j.neutral=now;if(j.good>=2 && (u32)(now-j.neutral)>=JT_MS(100))js.armed=1;}else j.neutral=0;}
 j.mask=mask;
 if(js.armed)player_input_local_pair(now,j.sample.issue[0],j.epoch,js.sequence,((mask&1)?2:0)|((mask&2)?8:0)|((mask&4)?1:0)|((mask&8)?4:0));
}
/* GHDA: called on every service of a latched driver (the latch is stamped on the first one) */
static void joy_auto(u64 now){
 if(!jrec.latch_seen){jrec.latch_seen=1;jrec.latches++;jrec.latch_since=now;jrec.next=now+JOY_AUTO_PERIOD;
  if(jrec.active){u64 d=now-jrec.t0;jrec.active=0;jrec.fail++;jrec.last_ticks=d>UINT32_MAX?UINT32_MAX:(u32)d;}}
 if(!js.enabled || (int64_t)(now-jrec.next)<0)return;
 if(jrec.consecutive>=3u){jrec.exhausted=1;return;}
 jrec.consecutive++;jrec.attempts++;jrec.active=1;jrec.t0=now;jrec.latch_seen=0;joystick_enable(now,1);
}
void joystick_service(u64 now){
 /* GHDA: 0x2C = controller in-frame timeout in core_clk cycles (100 MHz, 20-bit): 1000000 = 10 ms (was 500000 = 5 ms, shorter than the
  * pressure-mode service gaps). Replayed on every J_INIT, so boot, retries, KEY3 and automatic recovery all use 10 ms. */
 /* DROP only stops the master sequencer. Quiesce both byte channels before bus
  * recovery so an interrupted read cannot keep SCL stretched waiting for CPU data.
  * TX VALID+REPEAT with ENABLE=0 supplies a passive response; release overrides. */
 static const uint32_t init[][2]={{0x20,0},{0x28,23},{0x2c,1000000},{0x30,63},{0x50,519},{0x54,499},{0x58,519},{0x40,0x40},{0x00,0x500},{0x04,0x500},{0x08,0},{0x0c,0},{0x48,6}};
 u64 start,end,elapsed;u32 epoch=player_input_local_epoch();unsigned step;int r;
 if(j.lock)return;j.lock=1;start=clint_getTime(BSP_CLINT);j.mmio=j.steps=0;j.observed_at=now;
 if(j.state==J_LATCH){js.latched=1;js.last_service=now;joy_auto(now);goto done;} /* GHDA: automatic recovery attempt when due */
 j.service_gap_valid=js.last_service && now>=js.last_service;
 j.service_gap=j.service_gap_valid?sat_elapsed(now,js.last_service):0;
 jrec.calls++;if(j.service_gap>jrec.gap_win_max)jrec.gap_win_max=j.service_gap; /* GHDA step 6 */
 if(js.last_service && now>=js.last_service && now-js.last_service>js.max_gap){u64 gap=now-js.last_service;js.max_gap=gap>0xffffffffu?0xffffffffu:(u32)gap;}
 if(js.enabled && js.last_service && now>=js.last_service && now-js.last_service>=JT_MS(40))fault(now,9,D_SERVICE_GAP);
 if(now<js.last_service){if(js.enabled){fault(now,8,D_TIME_BACKWARD);jw(0x40,0x40);}permission(0);j.state=J_LATCH;}
 js.last_service=now;
 if(epoch!=j.epoch){j.epoch=epoch;js.valid=js.armed=j.mask=j.good=0;j.neutral=0;j.prev_issue=0;if(js.enabled && j.state<J_STOP){j.state=J_STOP;j.deadline=now+JT_MS(1);jw(0x40,0x20);}}
 if(!js.enabled)goto done;
 if(jrec.active && now-jrec.t0>=JOY_AUTO_CAP){permission(0);invalid(now);j.state=J_LATCH;js.latched=1;goto done;} /* GHDA: attempt cap -> latched (fail) */
 if(j.episode_on && (now<j.episode || now-j.episode>=JT_MS(2000)) && j.state!=J_LATCH){permission(0);invalid(now);j.state=J_LATCH;}
 for(step=0;step<4 && j.mmio<=4;step++){
  j.steps++; /* CMDJ: no mid-pair 10 ms wall-clock fault; publish() classifies the pair age */
  switch(j.state){
  case J_INIT:
   if(j.init<sizeof(init)/sizeof(init[0])){jw(init[j.init][0],init[j.init][1]);j.init++;}
   else if((int32_t)((u32)now-j.due)>=0){permission(1);
    /* Recover a pending frame OR low bus lines. The hardware timeout can clear
     * inFrame while the ADS1115 is still holding SDA in an interrupted read. */
    if((jr(0x44)&7u)!=6u){jw(0x40,0xe80);if(jrec.bus_recovers!=UINT32_MAX)jrec.bus_recovers++;j.deadline=now+JT_MS(3);j.state=J_BUSREC;}else j.state=J_IDLE;}break;
  case J_BUSREC: /* Require master idle, no frame, and both lines released. */
   if((jr(0x40)&0x81u) || ((jr(0x44)&7u)!=6u)){if((int32_t)((u32)now-j.deadline)>=0){fault(now,1,D_BUS_RECOVER);goto done;}goto done;}
   if(jrec.bus_recover_ok!=UINT32_MAX)jrec.bus_recover_ok++;j.state=J_IDLE;break;
  case J_IDLE:
   if((int32_t)((u32)now-j.due)<0)goto done;
   j.due=now+JT_MS(10);j.axis=0;j.sample.issue[0]=now;j.epoch=epoch;transaction(now,0);j.state=J_CONFIG;jrec.pair_starts++;break;
  case J_CONFIG:case J_READY:case J_READ:
   j.pump_reason=D_NONE;r=pump(now);if(r<0){fault(now,1,j.pump_reason);goto done;}if(!r)goto done;if(r==2)continue; /* CMDJ: next sub-step, same budget */
   if(j.state==J_CONFIG){j.sample.done[j.axis]=now;j.deadline=now+JT_MS(3);j.due=now+JT_US(1400);j.state=J_WAIT;}
   else if(j.state==J_READY){
    if((j.result&0x7fff)!=(j.axis?0x53e3:0x43e3)){fault(now,4,D_CONFIG_MISMATCH);goto done;}
    if(!(j.result&0x8000)){if((int32_t)((u32)now-j.deadline)>=0){fault(now,2,D_READY_TIMEOUT);goto done;}j.due=now+JT_US(250);j.state=J_WAIT;}
    else{j.sample.ready[j.axis]=now;transaction(now,2);j.state=J_READ;}
   }else{
    j.sample.raw[j.axis]=(int16_t)j.result;j.sample.read[j.axis]=now;
    if(j.axis==0){j.axis=1;j.sample.issue[1]=now;transaction(now,0);j.state=J_CONFIG;}
    else{publish(now);if(j.state==J_READ){j.state=J_IDLE;j.due=j.sample.issue[0]+JT_MS(10);if((int32_t)((u32)now-j.due)>=0)j.due=now;}} /* CMDJ: late pair -> next at once */
   }break;
  case J_WAIT:if((int32_t)((u32)now-j.due)<0)goto done;transaction(now,1);j.state=J_READY;break;
  case J_STOP:if((jr(0x40)&0x21) && (int32_t)((u32)now-j.deadline)<0)goto done;jw(0x40,0x40);j.state=J_DROP;break;
  case J_DROP:permission(0);if(!j.episode_on){j.init=0;j.state=J_INIT;j.due=now+JT_MS(1);break;}if(j.tries>=3){j.state=J_LATCH;break;}j.due=now+JT_MS(j.tries==0?100:j.tries==1?250:500);j.state=J_BACKOFF;goto done;
  case J_BACKOFF:if((int32_t)((u32)now-j.due)<0)goto done;if((jr(0x44)&6u)!=6u){if(jd.count[D_RECOVERY_LINES]!=UINT32_MAX)jd.count[D_RECOVERY_LINES]++;}j.tries++;js.probes++;j.init=0;j.state=J_INIT;j.due=now+JT_MS(1);break;
  case J_LATCH:js.latched=1;goto done;
  }
 }
done:
 if(j.mmio>js.max_mmio)js.max_mmio=j.mmio;if(j.steps>js.max_steps)js.max_steps=j.steps;
 end=clint_getTime(BSP_CLINT);elapsed=end>=start?end-start:UINT64_MAX;
 if(elapsed>js.max_service_ticks)js.max_service_ticks=elapsed>UINT32_MAX?UINT32_MAX:(u32)elapsed;
 j.lock=0;
}
