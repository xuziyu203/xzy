#include "game_lifecycle.h"
#include "gpu.h"
#include "framebuffer_q8.h"
#include "renderer.h"
#include "cpu_writer.h"
#include "asset_upload.h"
#include "enemy_test.h"
#include "bg_uart.h"
#include "bg_ddr_adapter.h"
#include "game_hud.h"
#include "uart_reply.h"
#include "bsp.h"
#include "io.h"

/* One transaction, no speculative game copies and no extra pixel buffer. */
static struct {
 uint32_t state,epoch,confirmed_epoch,base_generation,confirmed_generation;
 uint32_t error,first_error,first_stage,first_writer,first_handoff,first_events,first_generation;
 uint64_t since;
} idle;
static void (*idle_poll)(void);
static uint32_t bg_saved,bg_restore_epoch,bg_restore_state;
static uint32_t idle_retry,exit_reply_pending;

static uint32_t handoff(void){return read_u32(IO_APB_SLAVE_0_INPUT+GAME_ONLY_HANDOFF_OFFSET);}
static int supported(void){return framebuffer_q8_supported();}
static int hardware_quiet(int retry){
 uint32_t h=handoff(),w=cpu_writer_status();
 return supported() && (h&GAME_ONLY_INIT_DONE) && !(h&GAME_ONLY_BUSY_MASK) &&
  !(w&(CPU_WR_STATUS_FRAME_ACTIVE|CPU_WR_STATUS_ROW_ACTIVE|CPU_WR_STATUS_DDR_INFLIGHT)) &&
  (retry || !(w&CPU_WR_STATUS_ERROR)) &&
  (w&CPU_WR_STATUS_OWNER_READY) && (gpu_get_display_status()&15u)==8u;
}
static int hardware_safe(void){return hardware_quiet(0);}
static int elapsed(void){return clint_getTime(BSP_CLINT)-idle.since>5ull*BSP_CLINT_HZ;}
static void fail(uint32_t code){
 idle.error=code;
 if(!idle.first_error){idle.first_error=code;idle.first_stage=idle.state;
  idle.first_writer=cpu_writer_status();idle.first_handoff=handoff();
  idle.first_events=gpu_get_async_event_status();idle.first_generation=gpu_get_display_generation();}
 idle.state=GO_FAILED;
}
void game_lifecycle_init(void (*poll)(void)){
 idle_poll=poll;idle.epoch=1;idle.state=GO_WAIT;idle.since=clint_getTime(BSP_CLINT);
}
int game_lifecycle_safe(void){return idle.state==GO_IDLE && idle.confirmed_epoch==idle.epoch && hardware_safe();}
uint32_t game_idle_epoch(void){return idle.epoch;}
uint32_t game_idle_generation(uint32_t epoch){return idle.confirmed_epoch==epoch && game_lifecycle_safe()?idle.confirmed_generation:0;}
int game_lifecycle_busy(void){return (idle.state!=GO_IDLE && idle.state!=GO_GAME) || bg_saved;}
void game_lifecycle_service(void){
 uint32_t h;
 if(renderer_cpu_sync_waiting())return;
 if(idle.state==GO_FAILED){renderer_cpu_seed_drain();return;}
 if(idle.state!=GO_WAIT && idle.state!=GO_ACK)return;
 if(!supported()){fail(1);return;}
 h=handoff();
 if(h&GAME_ONLY_READ_ERROR){fail(2);return;}
 if(elapsed()){fail(3);return;}
 if(idle.state==GO_WAIT){
  if(asset_upload_active() || bg_ddr_adapter_active() || game_hud_releasing())return;
  /* No grant or ownership override: the real writer gate is authoritative. */
  renderer_cpu_seed_drain();
  if(!hardware_quiet(idle_retry!=0))return;
  idle_retry=0;
  idle.base_generation=gpu_get_display_generation();idle.state=GO_SEED;
  if(renderer_cpu_seed_black(idle_poll)!=0){fail(4);return;}
  idle.state=GO_ACK;idle.since=clint_getTime(BSP_CLINT);
 }
 if(idle.state==GO_ACK && hardware_safe() &&
    gpu_get_display_generation()!=idle.base_generation){
  idle.confirmed_generation=gpu_get_display_generation();
  idle.confirmed_epoch=idle.epoch;idle.state=GO_IDLE;
  /* Queued event, never interleaved with a binary upload response. STATUS
   * carries the complete immutable identity even if this notice is delayed. */
  if(!asset_upload_active() && !bg_uart_binary_active() && !bg_uart_recovery_active())
   uart_reply_event("GAME IDLE_CONFIRMED\r\n");
 }
 if(exit_reply_pending && game_lifecycle_safe()){
  exit_reply_pending=0;uart_reply_event("ENEMY EXITED\r\n");
 }
}
int game_prepare_surface(void){
 if(renderer_cpu_sync_waiting() || bg_saved)return 0;
 if(idle.state==GO_FAILED)return -1;
 if((idle.state==GO_IDLE || idle.state==GO_GAME) && hardware_safe()){
  idle.state=GO_GAME;return 1;
 }
 return 0;
}
void game_surface_snapshot(uint32_t *mode,uint32_t *source){*mode=0;*source=2;}
int game_request_idle(uint32_t mode,uint32_t source){
 if(mode!=0 || source!=2 || renderer_cpu_sync_waiting())return -1;
 if(idle.state==GO_WAIT || idle.state==GO_SEED || idle.state==GO_ACK)return -1;
 /* Called once after ENEMY drain. A failed request can be retried by explicit
  * EXIT; the first-failure snapshot and sticky hardware events are retained. */
 renderer_cpu_seed_drain();
 idle_retry=(idle.state==GO_FAILED);
 idle.epoch++;idle.error=0;idle.state=GO_WAIT;idle.since=clint_getTime(BSP_CLINT);
 return 0;
}
int game_inactive_exit(void){
 /* Explicit retry only; CLEAR_STATUS remains inside the normal writer BEGIN,
  * after every real owner/row/DDR protection has passed. Global EVENTS and the
  * first-failure snapshot are never cleared. */
 if(!supported() || bg_saved || asset_upload_active() || bg_ddr_adapter_active())return -1;
 if(game_lifecycle_safe())return 1;
 if(idle.state==GO_FAILED && game_request_idle(0,2)<0)return -1;
 if(idle.state!=GO_WAIT && idle.state!=GO_ACK && idle.state!=GO_SEED)return -1;
 exit_reply_pending=1;return 0;
}
int game_idle_confirmed(void){return idle.state==GO_FAILED?-1:game_lifecycle_safe();}
int game_asset_prepare(void){
 if(renderer_cpu_sync_waiting() || bg_saved || enemy_test_active())return 0;
 if(idle.state==GO_FAILED)return -1;
 return game_lifecycle_safe();
}
void game_asset_resume(void){/* Upload never changes the displayed surface. */}
int game_bg_prepare(void){
 if(renderer_cpu_sync_waiting() || enemy_test_active())return 0;
 if(idle.state==GO_FAILED)return -1;
 if(!bg_saved){if(!game_lifecycle_safe())return 0;bg_saved=1;bg_restore_state=BG_RESTORE_STATUS_NONE;}
 return hardware_safe();
}
int game_bg_restore_pending(void){return bg_saved!=0;}
int game_bg_restore(void){
 if(!bg_saved || renderer_cpu_sync_waiting())return -1;
 if(bg_restore_state==BG_RESTORE_STATUS_RESTORING)return 1;
 if(game_request_idle(0,2)<0)return -1;
 bg_restore_epoch=idle.epoch;bg_restore_state=BG_RESTORE_STATUS_RESTORING;return 0;
}
int game_bg_restore_status(void){
 if(bg_restore_state!=BG_RESTORE_STATUS_RESTORING)return (int)bg_restore_state;
 if(idle.state==GO_FAILED)bg_restore_state=BG_RESTORE_STATUS_FAILED;
 else if(idle.confirmed_epoch==bg_restore_epoch && game_lifecycle_safe()){
  bg_saved=0;bg_restore_state=BG_RESTORE_STATUS_RESTORED;
 }
 return (int)bg_restore_state;
}
static void dec(uint32_t x){char b[10];unsigned n=0;do{b[n++]=(char)('0'+x%10);x/=10;}while(x);while(n)uart_reply_write(BSP_UART_TERMINAL,b[--n]);}
void game_lifecycle_status(void){
#define D(k,v) do{uart_reply_writeStr(BSP_UART_TERMINAL,k);dec(v);}while(0)
#define H(k,v) do{uart_reply_writeStr(BSP_UART_TERMINAL,k);uart_reply_writeHex(BSP_UART_TERMINAL,(int)(v));}while(0)
 uart_reply_writeStr(BSP_UART_TERMINAL,"STATUS GAME_ONLY=1");H(" ABI=",read_u32(IO_APB_SLAVE_0_INPUT+GAME_ONLY_ABI_OFFSET));
 H(" FB_FMT=",read_u32(IO_APB_SLAVE_0_INPUT+0x310u));D(" STRIDE=",read_u32(IO_APB_SLAVE_0_INPUT+0x314u));D(" FRAME_BYTES=",read_u32(IO_APB_SLAVE_0_INPUT+0x318u));H(" CODEC=",read_u32(IO_APB_SLAVE_0_INPUT+0x31cu));H(" LANE=",read_u32(IO_APB_SLAVE_0_INPUT+0x320u));
 D(" STATE=",idle.state);D(" SAFE_IDLE=",game_lifecycle_safe());D(" EPOCH=",idle.epoch);D(" CONFIRMED_EPOCH=",idle.confirmed_epoch);
 D(" IDLE_GEN=",idle.confirmed_generation);D(" DISPLAY_GEN=",gpu_get_display_generation());
 H(" HANDOFF=",handoff());H(" CPU_STATUS=",cpu_writer_status());H(" DISPLAY=",gpu_get_display_status());
 H(" ERROR=",idle.error);H(" EVENTS=",gpu_get_async_event_status());H(" ERROR_INFO=",gpu_get_error_info());
 H(" FIRST_ERROR=",idle.first_error);D(" FIRST_STAGE=",idle.first_stage);H(" FIRST_WRITER=",idle.first_writer);
 H(" FIRST_HANDOFF=",idle.first_handoff);H(" FIRST_EVENTS=",idle.first_events);D(" FIRST_GEN=",idle.first_generation);
 uart_reply_writeStr(BSP_UART_TERMINAL,"\r\n");
#undef D
#undef H
}
