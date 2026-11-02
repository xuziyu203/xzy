#include "enemy_test.h"
#include "enemy_scene.h"
#include "bullet_scene.h"
#include "missile_scene.h"
#include "player_scene.h"
#include "player_input.h"
#include "player_bullet_scene.h"
#include "combat.h"
#include "effect_scene.h"
#include "decor_scene.h"
#include "joystick_ads1115.h"
#include "game_keys.h"
#include "game.h"
#include "game_lifecycle.h"
#include <stddef.h>
#include "uart_reply.h"
#include "game_hud.h"
#include "bullet_stats.h"
#include "build_id.h"
#include "asset_upload.h"
#include "bsp.h"
#include "soc.h"
#include "gpu.h"
#include "renderer.h"
#include <string.h>
enum { EN_IDLE, EN_PREPARING, EN_CLAIM, EN_DRAWING, EN_SHOWN,
       EN_DRAINING, EN_RESTORING, EN_EXITED, EN_FAILED, EN_RESTORE_FAILED, EN_RECYCLE };
static unsigned en_state,en_error,en_exit,en_started,en_restore_registered;
static uint32_t en_mode,en_source,en_generation;
static u32 en_background; /* desired session mode; BEGIN confirms actual PL config */
static u64 en_since;
/* No frame buffer here: one time remainder and one immutable submitted y. */
#define EN_SPEED 120u
#define EN_BOTTOM 1016u
#define EN_FRAME_TICKS (((u64)BSP_CLINT_HZ+59u)/60u)
/* O1: after a confirmed motion frame the next start waits only for that
 * confirmation; this floor rejects a confirmation arriving implausibly early. */
#define EN_START_MIN_TICKS ((u64)BSP_CLINT_HZ*8u/1000u)
static unsigned en_moving,en_stop_pending,en_y,en_frame_motion;
static unsigned en_split_seen,en_last_split,en_run_armed;
static uint32_t en_frames,en_run_frames,en_display_generation;
static u64 en_motion_time,en_fraction,en_due,en_submit_time,en_present_time,en_prep_start;
static uint32_t en_prep_us,en_prep_max_us;static unsigned en_prep_open;
static uint32_t en_hud_comp_max,en_hud_prep_max;
static uint32_t en_transition_us;static u64 en_transition_start; /* GHD4: light->pressure transition, kept out of L/X */
static u64 en_frame_ticks,en_comp_ticks,en_present_ticks,en_run_start,en_run_end;
static uint32_t en_intervals,en_gap25,en_interval_min,en_interval_max;
#ifndef BUILD_VARIANT_GAME
static uint32_t en_interval_us[64];
#endif
static enemy_test_hooks_t en_hooks;
static u32 app_state,app_internal; /* CMD application, see enemy_test_app_service() */
static u32 app_off_request,app_starts,app_restarts,app_errors;static u64 app_due;
static int app_owns_input(void){return app_state!=0u && !app_internal;}
static void en_diag_reset(void);
static const char *const en_names[]={"IDLE","PREPARING","CLAIM","DRAWING","SHOWN",
 "OWNER_HELD","RESTORING","EXITED","FAILED","RESTORE_FAILED","RECYCLE"};
static uint32_t en_read(unsigned r){return read_u32(IO_APB_SLAVE_0_INPUT+r);}
static void en_control(unsigned c){write_u32(c,IO_APB_SLAVE_0_INPUT+ENEMY_REG_CONTROL);}
#define FLOW_PB_KILLS 5u /* CMD: displayed player-bullet kills (one per enemy death) that start PRESSURE, once per round */
enum { FL_OFF,FL_LIGHT,FL_DRAIN,FL_BLACK_WAIT,FL_BLACK_HOLD,FL_PRESSURE_PREP,FL_PRESSURE_FIRST,FL_PRESSURE,FL_FAILED };
static struct {
 u64 remaining,last;
 u32 round,trigger_generation,black_epoch,black_generation,pressure_generation;
 uint8_t phase,enabled,triggered,paused,restarting,pad[7];
} flow;
typedef char flow_layout_check[(sizeof(flow)==48 && offsetof(__typeof__(flow),phase)==36)?1:-1];
static const char *const flow_names[]={"OFF","LIGHT","DRAIN","BLACK_WAIT","BLACK_HOLD","PRESSURE_PREP","PRESSURE_FIRST","PRESSURE","FAILED"};
static unsigned en_protocol_text,en_restart_pending;
static unsigned en_key_protocol_reply;
#ifdef BUILD_VARIANT_GAME
#define en_text(s) ((void)0) /* GHD3S: no serial text in the game build */
#else
static void en_text(const char*s){
 if(en_protocol_text)uart_writeStr(BSP_UART_TERMINAL,s);
 else uart_reply_event(s);
}
#endif
static void en_enter(unsigned s){en_state=s;en_since=clint_getTime(BSP_CLINT);
 if(s==EN_FAILED || s==EN_DRAINING || s==EN_RESTORE_FAILED)player_input_revoke(en_since);
}
static int en_timeout(void){return clint_getTime(BSP_CLINT)-en_since>(u64)BSP_CLINT_HZ*30u;}
void enemy_test_init(const enemy_test_hooks_t *h){memset(&en_hooks,0,sizeof(en_hooks));if(h)en_hooks=*h;
 en_state=EN_IDLE;en_error=en_exit=en_started=en_restore_registered=0;
 app_state=1u;app_internal=app_off_request=app_starts=app_restarts=app_errors=0;app_due=0; /* CMD: APP_WAIT at power-up */
 en_background=GPU_BACKGROUND_IMAGE;
 memset(&flow,0,sizeof(flow));en_key_protocol_reply=0;en_restart_pending=0;enemy_scene_init();bullet_scene_init();missile_scene_init();player_scene_init();joystick_init();game_keys_init();player_bullet_scene_init();effect_scene_init();decor_scene_init();game_init();combat_init();
 en_moving=en_stop_pending=en_y=en_frame_motion=en_frames=en_run_frames=en_display_generation=0;
 en_fraction=en_frame_ticks=en_comp_ticks=en_present_ticks=0;en_last_split=en_run_armed=0;
 en_run_start=en_run_end=en_motion_time=en_due=0;
 en_intervals=en_gap25=en_interval_min=en_interval_max=0;en_diag_reset();}
int enemy_test_active(void){return en_state!=EN_IDLE && en_state!=EN_EXITED;}
int enemy_test_blocks_scene(void){return enemy_test_active() && en_state!=EN_RESTORING;}
static int en_input_moving(void){return !game_over() && !en_restart_pending && !flow.restarting && (!flow.enabled || flow.phase==FL_LIGHT || flow.phase==FL_PRESSURE) && en_moving && en_frames && !en_exit && !en_error &&
 en_state!=EN_PREPARING &&
 enemy_scene_selected()!=ENEMY_SCENE_SINGLE && player_scene_selected();}
static int app_start_permit(void);
int enemy_test_input_moving_public(void){return en_input_moving();} /* GHD: menu-only calibration gate (game ELF) */
void enemy_test_input_service(void){u64 now=clint_getTime(BSP_CLINT);int permit=en_input_moving();if(game_over() && player_input_session())player_input_revoke(now);player_input_local_gate(now,permit);if(game_keys_hw_present())joystick_service(now);game_keys_service(now,permit,app_start_permit());player_input_service(now,permit);}
/* FIX: bounded joystick-only service between PREP stages (never the UART/display state machines). */
static __attribute__((noinline)) void en_prep_joy_tick(void){if(game_keys_hw_present())joystick_service(clint_getTime(BSP_CLINT));}
static __attribute__((noinline)) int en_prep_step(gpu_result_t r){if(r!=GPU_OK)return 0;en_prep_joy_tick();return 1;}
int enemy_test_input_line(const char *line){
 while(*line==' ' || *line=='\t')++line;
 return player_input_is_data(line);
}
static void en_decimal(u64 v){
 char b[21];unsigned n=0;
 /* O2a: identical digits; 32-bit divides avoid the 64-bit runtime helpers. */
 if(v<=0xffffffffu){u32 x=(u32)v;do{b[n++]=(char)('0'+x%10u);x/=10u;}while(x);}
 else do{b[n++]=(char)('0'+v%10u);v/=10u;}while(v);
 while(n)uart_reply_write(BSP_UART_TERMINAL,b[--n]);
}
static void en_hex4(u32 v){
 static const char hex[]="0123456789ABCDEF";unsigned n;
 for(n=4;n;n--)uart_reply_write(BSP_UART_TERMINAL,hex[(v>>((n-1u)*4u))&15u]);
}
static u64 en_us(u64 ticks){return (ticks/BSP_CLINT_HZ)*1000000u+
 (ticks%BSP_CLINT_HZ)*1000000u/BSP_CLINT_HZ;}
/* Stage-1 diagnostics (D). One 16-byte record per confirmed motion frame,
 * slot RUN_FRAMES-1 (INTERVALS index i ends at record i+1), and window
 * statistics since the last MOVE/BEGIN/RESTART. Observation only. */
#define EN_DIAG_RING 128u
#define EN_DIAG_TICKS_PER_US (BSP_CLINT_HZ/1000000u)
typedef char en_diag_tick_is_whole_us[(BSP_CLINT_HZ%1000000u==0u)?1:-1];
typedef struct { uint16_t lag,prep,comp,pres,cmd,visible,descriptors,stage_calls; } en_diag_frame_t;
typedef char en_diag_frame_is_16_bytes[(sizeof(en_diag_frame_t)==16)?1:-1];
#ifndef BUILD_VARIANT_GAME
static en_diag_frame_t en_diag_ring[EN_DIAG_RING];
#endif
static struct {
 u64 prep_sum,lag_sum,cmd_sum,wait_sum,drain_sum;
 u32 recorded,lag_count,prep_min,prep_max,lag_max,cmd_count,cmd_max,heavy_count,heavy_max,wait_max;
 u32 deferred,timeouts,priority,hold_max,txq_max,drain_count,drain_max,visible_min,visible_max,floor_hits;
 u64 w_prep_sum;u32 w_frames,w_visible_min,w_visible_max,w_prep_max;
} en_diag;
static u64 en_diag_cmd_ticks,en_diag_confirm_at,en_diag_drain_from;
static u32 en_diag_drain_target,en_diag_conf_frame,en_diag_conf_gen,en_diag_conf_seq,en_diag_conf_count;
static unsigned en_diag_drain_armed,en_diag_have_confirm;
static u32 en_diag_us32(u64 ticks){return ticks>=0xffffffffu?0xffffffffu/EN_DIAG_TICKS_PER_US:(u32)ticks/EN_DIAG_TICKS_PER_US;}
static uint16_t en_diag_us16(u64 ticks){u32 us=en_diag_us32(ticks);return us>65535u?65535u:(uint16_t)us;}
static void en_diag_reset(void){
 memset(&en_diag,0,sizeof(en_diag));en_diag.prep_min=en_diag.visible_min=en_diag.w_visible_min=0xffffffffu;
 en_diag_cmd_ticks=0;en_diag_have_confirm=0;
}
/* Called once per actual display confirmation, after its statistics are published. */
#ifdef BUILD_VARIANT_GAME
static void en_diag_confirm(u64 now){(void)now;} /* GHD3S: the FRAMES ring is a serial diagnostic */
#else
static void en_diag_confirm(u64 now){
 en_diag_conf_frame=en_frames;en_diag_conf_gen=en_display_generation;
 en_diag_conf_seq=bullet_scene_confirmed_sequence();en_diag_conf_count=bullet_scene_confirmed_count();
 if(!en_frame_motion || !en_run_frames)return;
 {en_diag_frame_t *d=&en_diag_ring[(en_run_frames-1u)%EN_DIAG_RING];u32 v=bullet_scene_displayed_visible(),c=en_diag_conf_count;
  u32 lag=en_diag_have_confirm && en_prep_start>=en_diag_confirm_at?en_diag_us32(en_prep_start-en_diag_confirm_at):0xffffffffu;
  d->lag=lag>65535u?65535u:(uint16_t)lag;d->prep=en_prep_us>65535u?65535u:(uint16_t)en_prep_us;
  d->comp=en_diag_us16(en_comp_ticks);d->pres=en_diag_us16(en_present_ticks);d->cmd=en_diag_us16(en_diag_cmd_ticks);
  d->visible=v>65535u?65535u:(uint16_t)v;d->descriptors=c>65535u?65535u:(uint16_t)c;
  c=bullet_scene_confirmed_stage_calls();d->stage_calls=c>65535u?65535u:(uint16_t)c;
  en_diag.recorded=en_run_frames;en_diag.prep_sum+=en_prep_us;
  if(en_prep_us<en_diag.prep_min)en_diag.prep_min=en_prep_us;
  if(en_prep_us>en_diag.prep_max)en_diag.prep_max=en_prep_us;
  if(lag!=0xffffffffu){en_diag.lag_count++;en_diag.lag_sum+=lag;if(lag>en_diag.lag_max)en_diag.lag_max=lag;}
  if(v<en_diag.visible_min)en_diag.visible_min=v;
  if(v>en_diag.visible_max)en_diag.visible_max=v;
  /* Stable window: same 8 s RUN_US boundary as the acceptance sampler. */
  if(en_run_end-en_run_start>=(u64)BSP_CLINT_HZ*8u){en_diag.w_frames++;en_diag.w_prep_sum+=en_prep_us;
   if(en_prep_us>en_diag.w_prep_max)en_diag.w_prep_max=en_prep_us;
   if(v<en_diag.w_visible_min)en_diag.w_visible_min=v;if(v>en_diag.w_visible_max)en_diag.w_visible_max=v;}
  en_diag_cmd_ticks=0;en_diag_confirm_at=now;en_diag_have_confirm=1;}
}
#endif
void enemy_test_diag_command(unsigned heavy,uint64_t ready_at,uint64_t start,uint64_t end,unsigned queued_before,unsigned queued_after){
 u32 wait=en_diag_us32(start>=ready_at?start-ready_at:0),cpu=en_diag_us32(end>=start?end-start:0);
 en_diag_cmd_ticks+=end>=start?end-start:0;en_diag.cmd_count++;en_diag.cmd_sum+=cpu;if(cpu>en_diag.cmd_max)en_diag.cmd_max=cpu;
 en_diag.wait_sum+=wait;if(wait>en_diag.wait_max)en_diag.wait_max=wait;
 if(!heavy)return;
 en_diag.heavy_count++;if(cpu>en_diag.heavy_max)en_diag.heavy_max=cpu;
 if(queued_before>en_diag.txq_max)en_diag.txq_max=queued_before;
 /* Commit -> last byte handed to the UART: ring residency only, not wire time. */
 if(queued_after>queued_before){en_diag_drain_target=uart_reply_sent_total()+queued_after;en_diag_drain_from=end;en_diag_drain_armed=1;}
}
void enemy_test_diag_event(unsigned kind,uint32_t value){
 if(kind==EN_DIAG_DEFERRED)en_diag.deferred++;
 else if(kind==EN_DIAG_TIMEOUT)en_diag.timeouts++;
 else if(kind==EN_DIAG_PRIORITY){en_diag.priority++;if(value>en_diag.hold_max)en_diag.hold_max=value;}
 else if(kind==EN_DIAG_FLOOR)en_diag.floor_hits++;
}
void enemy_test_diag_tx_poll(void){
 if(en_diag_drain_armed && (int32_t)(uart_reply_sent_total()-en_diag_drain_target)>=0){
  u32 d=en_diag_us32(clint_getTime(BSP_CLINT)-en_diag_drain_from);
  en_diag_drain_armed=0;en_diag.drain_count++;en_diag.drain_sum+=d;if(d>en_diag.drain_max)en_diag.drain_max=d;
 }
}
#ifndef BUILD_VARIANT_GAME /* GHD: UART-only */
/* Values are produced one at a time so the query keeps a small stack frame. */
static u64 en_frames_stat(unsigned i){
 switch(i){
 case 0:return en_diag.recorded;case 1:return en_run_frames;case 2:return en_diag.recorded?en_diag.prep_min:0;
 case 3:return en_diag.prep_max;case 4:return en_diag.prep_sum;case 5:return en_diag.lag_count;case 6:return en_diag.lag_max;
 case 7:return en_diag.lag_sum;case 8:return en_diag.cmd_count;case 9:return en_diag.cmd_max;case 10:return en_diag.cmd_sum;
 case 11:return en_diag.heavy_count;case 12:return en_diag.heavy_max;case 13:return en_diag.wait_max;case 14:return en_diag.wait_sum;
 case 15:return en_diag.deferred;case 16:return en_diag.timeouts;case 17:return en_diag.priority;case 18:return en_diag.hold_max;
 case 19:return en_diag.txq_max;case 20:return en_diag.drain_count;case 21:return en_diag.drain_max;case 22:return en_diag.drain_sum;
 case 23:return en_diag.recorded?en_diag.visible_min:0;case 24:return en_diag.visible_max;case 25:return en_diag.floor_hits;
 case 27:return en_diag_conf_frame;case 28:return en_diag_conf_gen;case 29:return en_diag_conf_seq;case 30:return en_diag_conf_count;
 case 31:return en_read(0x264);case 32:return en_read(0x268);case 33:return gpu_get_display_generation();case 34:return en_moving;
 case 35:return en_diag.w_frames;case 36:return en_diag.w_frames?en_diag.w_visible_min:0;case 37:return en_diag.w_visible_max;
 case 38:return en_diag.w_prep_max;case 39:return en_diag.w_prep_sum;default:return 0;
 }
}
static void en_frames_stats(void){
 static const char *const name[]={"RECORDED","RUN_FRAMES","PREP_MIN","PREP_MAX","PREP_SUM","LAG_N","LAG_MAX","LAG_SUM",
  "CMD_N","CMD_MAX","CMD_SUM","HEAVY_N","HEAVY_MAX","WAIT_MAX","WAIT_SUM","DEFERRED","DEFER_TIMEOUT","PRIORITY","HOLD_MAX",
  "TXQ_MAX","DRAIN_N","DRAIN_MAX","DRAIN_SUM","VISIBLE_MIN","VISIBLE_MAX","FLOOR_HITS","LCG0","CONF_FRAME","CONF_GEN",
  "CONF_BSEQ","CONF_BCOUNT","HW_BSEQ","HW_BLIVE","DISPLAY_GEN","MOVING","W_FRAMES","W_VIS_MIN","W_VIS_MAX","W_PREP_MAX","W_PREP_SUM"};
 unsigned i;
 en_text("ENEMY FRAMES STATS ABI=46520200");
 for(i=0;i<sizeof(name)/sizeof(name[0]);i++){
  en_text(" ");en_text(name[i]);en_text("=");
  if(i==26)uart_reply_writeHex(BSP_UART_TERMINAL,(int)bullet_scene_lcg_at_begin());else en_decimal(en_frames_stat(i));
 }
 en_text(" STATE=");en_text(en_names[en_state]);en_text("\r\n");
}
/* FRAMES <first>: up to 32 retained records, 8 fixed-width hex u16 fields each:
 * lag,prep,comp,pres,cmd (us), visible members, descriptors, stage calls. */
static void en_frames_query(const char *q){
 u32 start=0,first,n,i;
 while(*q==' ')q++;
 if(strcmp(q,"STATS")==0){en_frames_stats();return;}
 if(!*q){en_text("ENEMY FRAMES ERROR PARAM\r\n");return;}
 while(*q>='0' && *q<='9' && start<=100000000u){start=start*10u+(u32)(*q-'0');q++;}
 if(*q){en_text("ENEMY FRAMES ERROR PARAM\r\n");return;}
 first=en_diag.recorded>EN_DIAG_RING?en_diag.recorded-EN_DIAG_RING:0;if(start>first)first=start;
 if(first>en_diag.recorded)first=en_diag.recorded;
 n=en_diag.recorded-first;if(n>32u)n=32u;
 en_text("ENEMY FRAMES ABI=46520100 TOTAL=");en_decimal(en_diag.recorded);en_text(" FIRST=");en_decimal(first);
 en_text(" N=");en_decimal(n);en_text(" HEX=");
 for(i=first;i<first+n;i++){const en_diag_frame_t *d=&en_diag_ring[i%EN_DIAG_RING];
  en_hex4(d->lag);en_hex4(d->prep);en_hex4(d->comp);en_hex4(d->pres);en_hex4(d->cmd);en_hex4(d->visible);en_hex4(d->descriptors);en_hex4(d->stage_calls);}
 en_text("\r\n");
}
#endif /* !BUILD_VARIANT_GAME */
static int en_after_enemy(const char *s,const char **rest){
 if(strncmp(s,"ENEMY ",6))return 0;
 s+=6;while(*s==' ')s++;*rest=s;return 1;
}
int enemy_test_is_heavy_query(const char *s){
 static const char *const q[]={"STATUS","INTERVALS","PERF","EFFECTS","MISSILES STATUS","FLOW","FRAMES STATS","RICH","DECOR","PIPE","RICH POOL"};
 const char *r;unsigned i;
 if(strcmp(s,"STATUS")==0)return 1;
 if(!en_after_enemy(s,&r))return 0;
 for(i=0;i<sizeof(q)/sizeof(q[0]);i++)if(strcmp(r,q[i])==0)return 1;
 if(strncmp(r,"FRAMES ",7)==0){r+=7;while(*r==' ')r++;if(*r<'0' || *r>'9')return 0;while(*r>='0' && *r<='9')r++;return !*r;}
 return 0;
}
int enemy_test_query_busy(void){
 if(!enemy_test_active() || !en_moving || en_stop_pending || en_exit || en_error || renderer_cpu_sync_waiting())return 0;
 if(flow.enabled && flow.phase!=FL_LIGHT && flow.phase!=FL_PRESSURE)return 0;
 switch(en_state){
 case EN_CLAIM:return en_prep_open;
 case EN_DRAWING:return en_split_seen;
 case EN_SHOWN:case EN_RECYCLE:return 1;
 default:return 0;
 }
}
int enemy_test_is_priority_line(const char *s){
 const char *r;while(*s==' ' || *s=='\t')s++;
 return en_after_enemy(s,&r) && (strcmp(r,"STOP")==0 || strcmp(r,"EXIT")==0);
}
/* S: CPU computation of frame N+1 overlapped with PL composition of N (schedule/SCHEDULE_DESIGN_zh.md).
 * en_pipe: ENEMY PIPE ON|OFF (idle only, default OFF). en_pipe_ready: N+1 is computed (CPU buffers only) and not
 * yet published. FLOW keeps the serial schedule. */
static u32 en_pipe_staged; /* GHD3S: N+1 tables already written and sealed into the stage banks during N */
static u32 en_pipe,en_pipe_ready,en_pipe_lcg,en_pipe_preps,en_pipe_frames,en_pipe_discards,en_pipe_prep_max_us;
static u64 en_pipe_prep_sum_us;
static int en_pipe_on(void){return en_pipe && enemy_scene_selected() && (!flow.enabled || flow.phase==FL_PRESSURE);} /* CMD: FLOW pressure uses PIPE */
static void en_pipe_claimed(void){enemy_scene_claimed();bullet_scene_claimed();missile_scene_claimed();player_scene_claimed();
 player_bullet_scene_claimed();effect_scene_claimed();decor_scene_claimed();combat_claimed();game_claimed();}
/* Drop a computed, unpublished frame (RESTART/EXIT/BEGIN): restore the LCG so it is not advanced by a frame never shown. */
static void en_pipe_discard(void){en_pipe_staged=0;if(en_pipe_ready){bullet_scene_lcg_restore(en_pipe_lcg);en_pipe_ready=0;en_pipe_discards++;}player_scene_defer(0);player_scene_drop();}
static void en_stop_done(void){
 en_motion_time=clint_getTime(BSP_CLINT);
 enemy_scene_reset_time(en_motion_time);bullet_scene_reset_time(en_motion_time);missile_scene_reset_time(en_motion_time);decor_scene_reset_time(en_motion_time);
 player_bullet_scene_reset_time(en_motion_time);effect_scene_reset_time(en_motion_time);game_reset_time(en_motion_time);
 if(en_stop_pending){en_stop_pending=0;en_text("ENEMY STOPPED\r\n");}
}
static void en_move_start(void){
 u64 now=clint_getTime(BSP_CLINT);
 player_input_revoke(now);
 enemy_scene_reset_time(now);bullet_scene_reset_time(now);missile_scene_reset_time(now);decor_scene_reset_time(now);
 player_bullet_scene_reset_time(now);effect_scene_reset_time(now);game_reset_time(now);
 en_moving=1;en_stop_pending=0;en_motion_time=en_run_start=now;en_run_end=now;
 en_run_frames=0;en_run_armed=0;en_due=now+EN_FRAME_TICKS;
 en_intervals=en_gap25=en_interval_min=en_interval_max=0;en_prep_max_us=0;en_diag_reset();
}
static void en_next_frame(void){
 u64 now=clint_getTime(BSP_CLINT),elapsed=now-en_motion_time;
 en_prep_start=now;en_prep_open=1;
 if(enemy_scene_selected()){
  /* PL configuration requires both descriptor ports to still be idle. */
  /* Order: game clock, player, enemies, both bullet pools move (no new enemy
   * fire yet), effects age; then one combat pass; then deaths are removed,
   * enemy fire uses the surviving emitters, and every table is collected. */
  game_advance(now);
  if(game_keys_take_shield() && en_input_moving() && !game_over() && !en_restart_pending)(void)effect_shield_toggle(); /* GHD: KEY2 toggles */
  player_input_local_gate(now,en_input_moving() && !game_over() && !en_restart_pending);
  player_bullet_scene_set_fire(!game_over());enemy_scene_set_respawn(!game_over());
  if(!en_prep_step(player_scene_next(now)) || !en_prep_step(enemy_scene_advance(now)) || !en_prep_step(player_bullet_scene_advance(now)) ||
     !en_prep_step(bullet_scene_advance_motion(now)) || !en_prep_step(missile_scene_advance_motion(now)) || !en_prep_step(effect_scene_advance(now))){en_error=7;en_moving=0;en_enter(EN_FAILED);return;}
  combat_begin_frame();combat_resolve(now);
  if(game_over())player_input_local_gate(clint_getTime(BSP_CLINT),0);
  en_prep_joy_tick();
  bullet_scene_set_fire(!game_over());missile_scene_set_fire(!game_over());
  if(!en_prep_step(enemy_scene_collect()) || !en_prep_step(bullet_scene_advance_fire(now)) || !en_prep_step(missile_scene_advance_fire(now)) || !en_prep_step(missile_scene_collect()) || !en_prep_step(bullet_scene_collect()) ||
     !en_prep_step(player_bullet_scene_collect()) || !en_prep_step(effect_scene_collect())){en_error=7;en_moving=0;en_enter(EN_FAILED);return;}
  decor_scene_advance(now);decor_scene_collect(); /* own tick clock; slot words latch at this frame's claim */
 }else{
 u64 scaled=(elapsed%BSP_CLINT_HZ)*EN_SPEED+en_fraction;
 u64 pixels=(elapsed/BSP_CLINT_HZ)*EN_SPEED+scaled/BSP_CLINT_HZ;
 en_motion_time=now;en_fraction=scaled%BSP_CLINT_HZ;
 /* Never skip the fully visible bottom or top frame, even after a stall. */
 if(en_y==EN_BOTTOM){en_y=0;en_fraction=0;}
 else if(pixels>=EN_BOTTOM-en_y){en_y=EN_BOTTOM;en_fraction=0;}
 else en_y+=(unsigned)pixels;
 }
 /* O1: en_due is set from this frame's display confirmation (EN_DRAWING).
  * No backlog is kept: at most one start per confirmation; motion keeps
  * using each object's real dt, so a late frame is not compensated. */
 en_generation=gpu_get_display_generation();
 en_frame_motion=1;en_split_seen=0;
 write_u32(en_y,IO_APB_SLAVE_0_INPUT+ENEMY_REG_Y);
 en_enter(EN_CLAIM);
}
/* S: compute N+1 in EN_DRAWING right after claim N was accepted. Same order and calls as en_next_frame()
 * (multi mode), but no PL write: the player word is held, decor slot words are written by en_pipe_publish(). */
static enemy_test_telem_t en_tel; /* GHDA: defined here, used by en_pipe_prep and the shown-frame accumulators */
static void en_pipe_prep(void){
 u64 now=clint_getTime(BSP_CLINT),us;int bad;
 en_pipe_lcg=bullet_scene_lcg();
 game_advance(now);
 if(game_keys_take_shield() && en_input_moving() && !game_over() && !en_restart_pending)(void)effect_shield_toggle(); /* GHD: KEY2 toggles */
 player_input_local_gate(now,en_input_moving() && !game_over() && !en_restart_pending);
 player_bullet_scene_set_fire(!game_over());enemy_scene_set_respawn(!game_over());
 player_scene_defer(1);
 bad=!en_prep_step(player_scene_next(now)) || !en_prep_step(enemy_scene_advance(now)) || !en_prep_step(player_bullet_scene_advance(now)) ||
     !en_prep_step(bullet_scene_advance_motion(now)) || !en_prep_step(missile_scene_advance_motion(now)) || !en_prep_step(effect_scene_advance(now));
 player_scene_defer(0);
 if(bad){en_error=7;en_moving=0;en_enter(EN_FAILED);return;}
 combat_begin_frame();combat_resolve(now);
 if(game_over())player_input_local_gate(clint_getTime(BSP_CLINT),0);
 en_prep_joy_tick();
 bullet_scene_set_fire(!game_over());missile_scene_set_fire(!game_over());
 if(!en_prep_step(enemy_scene_collect()) || !en_prep_step(bullet_scene_advance_fire(now)) || !en_prep_step(missile_scene_advance_fire(now)) || !en_prep_step(missile_scene_collect()) || !en_prep_step(bullet_scene_collect()) ||
    !en_prep_step(player_bullet_scene_collect()) || !en_prep_step(effect_scene_collect())){en_error=7;en_moving=0;en_enter(EN_FAILED);return;}
 decor_scene_advance(now);
 en_pipe_ready=1;en_pipe_preps++;
 us=en_us(clint_getTime(BSP_CLINT)-now);if(us>en_pipe_prep_max_us)en_pipe_prep_max_us=us>0xffffffffu?0xffffffffu:(u32)us;en_pipe_prep_sum_us+=us;
 {u32 u=us>0xffffffffu?0xffffffffu:(u32)us;if(u>en_tel.pmax_us)en_tel.pmax_us=u;} /* GHDA step 6: pipelined prep max per telemetry second */
}
/* S: after N is released (PL idle): player word, decor slot words, Y, then the usual EN_CLAIM (stage + seal + claim).
 * PREP in the FRAMES ring is therefore release -> claim (the critical-path part); the overlapped part is in ENEMY PIPE. */
/* GHD3S: one bounded staging round of the computed frame into the stage banks: 1 all five sealed, 0 more to do, -1 error. */
static int en_pipe_stage_round(void){
 int r=enemy_scene_stage_service();
 if(r==1)r=bullet_scene_stage_service();
 if(r==1)r=player_bullet_scene_stage_service();
 if(r==1)r=effect_scene_stage_service();
 if(r==1)r=missile_scene_stage_service();
 if(r==1)en_pipe_staged=1;
 return r;
}
static void en_pipe_publish(void){
 en_prep_start=clint_getTime(BSP_CLINT);en_prep_open=1;
 if(player_scene_apply()!=GPU_OK){en_error=7;en_moving=0;en_enter(EN_FAILED);return;}
 decor_scene_collect();
 en_pipe_ready=0;en_pipe_staged=0;en_pipe_frames++;
 en_generation=gpu_get_display_generation();
 en_frame_motion=1;en_split_seen=0;
 write_u32(en_y,IO_APB_SLAVE_0_INPUT+ENEMY_REG_Y);
 en_enter(EN_CLAIM);
}
/* GHD: projectile statistics of the shown frame and a measurement window (STATS RESET starts one; frames and time are
 * counted over the same window, FPS_X100 = shown new frames per second x 100). Read-only otherwise. */
static u64 en_stats_t0;
static void en_stats_session(u64 now){bullet_stats_reset();en_stats_t0=now;}
/* GHDA telemetry: shown-frame accumulators for the per-second UART line (main_single.c); observation only */
static void en_tel_frame(uint32_t iv,uint32_t c_us,uint32_t r_us){
 const bullet_stats_t *s=bullet_stats_displayed();
 if(!en_tel.frames || s->total<en_tel.bul_min)en_tel.bul_min=s->total;
 en_tel.frames++;if(iv>25000u)en_tel.long_frames++;if(iv>en_tel.imax_us)en_tel.imax_us=iv;
 if(c_us>en_tel.cmax_us)en_tel.cmax_us=c_us;if(r_us>en_tel.rmax_us)en_tel.rmax_us=r_us;
 en_tel.bul_sum+=s->total;if(s->total>en_tel.bul_max)en_tel.bul_max=s->total;
 en_tel.grp_sum+=s->groups;if(s->groups>en_tel.grp_max)en_tel.grp_max=s->groups;
 en_tel.glow_sum+=s->turret;if(s->turret>en_tel.glow_max)en_tel.glow_max=s->turret;
 en_tel.stress=flow.enabled && (flow.phase==FL_PRESSURE_FIRST || flow.phase==FL_PRESSURE);
}
void enemy_test_telem_take(enemy_test_telem_t *t){*t=en_tel;memset(&en_tel,0,sizeof(en_tel));}
static void en_hud2_fields(void){ /* GHD8: MODE of the shown frame from the game phase only (STRESS from the first pressure frame on;
  game over keeps the phase, so the mode before the end stays; new game / restart = FL_LIGHT) */
 game_hud_set_hud2(enemy_scene_alive_count(),bullet_stats_displayed()->total,(effect_scene_snapshot()->flags>>16)&1u,
                   (flow.enabled && (flow.phase==FL_PRESSURE_FIRST || flow.phase==FL_PRESSURE))?GAME_HUD_MODE_STRESS:GAME_HUD_MODE_LIGHT);
}
#ifndef BUILD_VARIANT_GAME /* GHD: UART-only */
static void en_avg(const char *name,u64 sum,u32 n){en_text(name);en_decimal(n?(sum*100u+n/2u)/n:0u);}
static void en_stats(int reset){
 const bullet_stats_t *s=bullet_stats_displayed();const bullet_stats_window_t *w=bullet_stats_window();
 u64 now=clint_getTime(BSP_CLINT),us=en_us(now-en_stats_t0);
 en_text("ENEMY STATS AIR=");en_decimal(s->air);en_text(" MISSILE=");en_decimal(s->missile);en_text(" TURRET=");en_decimal(s->turret);
 en_text(" PLAYER=");en_decimal(s->player);en_text(" ENEMY_TOTAL=");en_decimal(s->enemy);en_text(" TOTAL=");en_decimal(s->total);
 en_text(" GROUPS=");en_decimal(s->groups);en_text(" ENEMIES=");en_decimal(enemy_scene_alive_count());
 en_text(" HP=");en_decimal(game_displayed_lives());en_text(" OVER=");en_decimal(game_displayed_over());
 en_text(" SHIELD_SHOWN=");en_decimal((effect_scene_snapshot()->flags>>16)&1u);en_text(" SHIELD_ON=");en_decimal(effect_shield_on());
 en_text(" TOGGLES=");en_decimal(effect_shield_toggles());en_text(" ABSORBED=");en_decimal(effect_shield_absorbed());
 en_text(" HUD2=");en_decimal((u32)game_hud_is_hud2());
 en_text(" WIN_US=");en_decimal(us);en_text(" WIN_FRAMES=");en_decimal(w->frames);
 en_text(" FPS_X100=");en_decimal(us?((u64)w->frames*100000000u+us/2u)/us:0u);
 en_text(" TOTAL_MIN=");en_decimal(w->frames?w->total_min:0u);en_text(" TOTAL_MAX=");en_decimal(w->total_max);
 en_avg(" TOTAL_AVG_X100=",w->total_sum,w->frames);en_avg(" AIR_AVG_X100=",w->air_sum,w->frames);
 en_avg(" MISSILE_AVG_X100=",w->missile_sum,w->frames);en_avg(" TURRET_AVG_X100=",w->turret_sum,w->frames);
 en_avg(" PLAYER_AVG_X100=",w->player_sum,w->frames);en_text(" AIR_MAX=");en_decimal(w->air_max);en_text(" TURRET_MAX=");en_decimal(w->turret_max);
 en_text("\r\n");
 if(reset){bullet_stats_window_reset();en_stats_t0=now;}
}
static void en_status(void){
 /* Completed motion frames and their matching completed window only.
  * STOP/EXIT/retry/query cannot extend this window without a new frame. */
 u64 elapsed=en_us(en_run_end-en_run_start);
 u32 key_config=0,key_frame=0;
 u32 bg_config=0,bg_frame=0,bg_error=0;
 const unsigned bg_supported=gpu_background_get(&bg_config,&bg_frame,&bg_error)==GPU_OK;
 const unsigned key_supported=gpu_enemy_key_get(&key_config,&key_frame)==GPU_OK;
 const uint32_t cap=en_read(0x1e8),hw=en_read(ENEMY_REG_STATUS),
  phase=en_read(ENEMY_REG_PHASE),back=en_read(ENEMY_REG_BACK),bytes=en_read(ENEMY_REG_BYTES);
 en_text("ENEMY STATUS STATE=");en_text(en_names[en_state]);en_text(" ERROR=");
 uart_reply_writeHex(BSP_UART_TERMINAL,(int)en_error);
 en_text(" CAP=");uart_reply_writeHex(BSP_UART_TERMINAL,(int)cap);
 en_text(" HW=");uart_reply_writeHex(BSP_UART_TERMINAL,(int)hw);
 en_text(" PHASE=");uart_reply_writeHex(BSP_UART_TERMINAL,(int)phase);
 en_text(" BACK=");uart_reply_writeHex(BSP_UART_TERMINAL,(int)back);
 en_text(" BYTES=");uart_reply_writeHex(BSP_UART_TERMINAL,(int)bytes);
 en_text(" SCENE=");en_text(enemy_scene_selected()?"BOSS4":"SINGLE");
 en_text(" OBJECTS=");en_decimal(enemy_scene_displayed_objects()+bullet_scene_displayed_visible()+missile_scene_displayed_visible()+player_scene_displayed_visible()+player_bullet_scene_displayed_visible());
 en_text(" ENEMIES=");en_decimal(enemy_scene_displayed_objects());
 /* MS1 fixed four hex nibbles: low to high BA/BV/RA/RV, each 0..4.
  * All four counters belong to this confirmed FRAMES snapshot. */
 en_text(" MS=");en_hex4(missile_scene_displayed_type(0,0)|
   (missile_scene_displayed_type(0,1)<<4)|(missile_scene_displayed_type(1,0)<<8)|(missile_scene_displayed_type(1,1)<<12));
 en_text(" BULLETS_ACTIVE=");en_decimal(bullet_scene_displayed_active());
 en_text(" BULLETS_VISIBLE=");en_decimal(bullet_scene_displayed_visible());
 en_text(" BULLETS_ENABLED=");en_decimal(bullet_scene_selected());
 en_text(" BULLETS_SPAWNED=");en_decimal(bullet_scene_displayed_spawned());
 en_text(" BULLETS_DROP_FULL=");en_decimal(bullet_scene_displayed_dropped());
 en_text(" BULLETS_SKIP_OFFSCREEN=");en_decimal(bullet_scene_displayed_skipped());
 en_text(" PLAYER_ENABLED=");en_decimal(player_scene_selected());
 en_text(" PLAYER_VISIBLE=");en_decimal(player_scene_displayed_visible());
 en_text(" PLAYER_X=");en_decimal(player_scene_displayed_x());
 en_text(" PLAYER_Y=");en_decimal(player_scene_displayed_y());
 en_text(" PB_ENABLED=");en_decimal(player_bullet_scene_selected());
 en_text(" PB_ACTIVE=");en_decimal(player_bullet_scene_displayed_active());
 en_text(" PB_VISIBLE=");en_decimal(player_bullet_scene_displayed_visible());
 en_text(" PB_SPAWNED=");en_decimal(player_bullet_scene_displayed_spawned());
 en_text(" PB_DROP_FULL=");en_decimal(player_bullet_scene_displayed_dropped());
 en_text(" PB_SKIP_OFFSCREEN=");en_decimal(player_bullet_scene_displayed_skipped());
 en_text(" ENEMY_CAPACITY=");en_decimal(ENEMY_CAPACITY);
 en_text(" MOVING=");en_decimal(en_moving);en_text(" X=928 Y=");en_decimal(en_y);
 en_text(" SPEED=120 FRAMES=");en_decimal(en_frames);
 en_text(" RUN_FRAMES=");en_decimal(en_run_frames);en_text(" RUN_US=");en_decimal(elapsed);
 en_text(" FPS_MILLI=");en_decimal(elapsed?(u64)en_run_frames*1000000000u/elapsed:0);
 en_text(" FRAME_US=");en_decimal(en_us(en_frame_ticks));
 en_text(" OBS_COMPOSE_US=");en_decimal(en_us(en_comp_ticks));
 en_text(" OBS_PRESENT_US=");en_decimal(en_us(en_present_ticks));
 en_text(" SPLIT_SAMPLED=");en_decimal(en_last_split);
 en_text(" INTERVALS=");en_decimal(en_intervals);en_text(" MIN_US=");en_decimal(en_interval_min);
 en_text(" MAX_US=");en_decimal(en_interval_max);en_text(" GAP_GT25MS=");en_decimal(en_gap25);
 en_text(" RESTORE_MODE=");en_decimal(en_mode);en_text(" RESTORE_SOURCE=");en_decimal(en_source);
 en_text(" RESTORE_REGISTERED=");en_decimal(en_restore_registered);
 en_text(" KEY_SUPPORTED=");en_decimal(key_supported);
 en_text(" KEY_CONFIG=");uart_reply_writeHex(BSP_UART_TERMINAL,(int)key_config);
 en_text(" KEY_FRAME_VALID=");en_decimal((key_frame&GPU_ENEMY_KEY_FRAME_VALID)!=0);
 en_text(" KEY_FRAME_CONFIG=");uart_reply_writeHex(BSP_UART_TERMINAL,(int)(key_frame&GPU_ENEMY_KEY_CONFIG_MASK));
 en_text(" BG_SUPPORTED=");en_decimal(bg_supported);en_text(" BG_CONFIG=");en_decimal(bg_config);
 en_text(" BG_FRAME_VALID=");en_decimal((bg_frame&GPU_BACKGROUND_FRAME_VALID)!=0);
 en_text(" BG_FRAME=");en_decimal(bg_frame&1u);en_text(" BG_ERROR=");en_decimal(bg_error);
 en_text(" WAVE=");en_text(enemy_scene_name());
 en_text(" GAME=");en_decimal(game_selected());en_text(" LIVES=");en_decimal(game_displayed_lives());
 en_text(" SCORE=");en_decimal(game_displayed_score());en_text(" GAME_OVER=");en_decimal(game_displayed_over());
 en_text(" INVULNERABLE=");en_decimal(game_displayed_invulnerable());en_text(" SURVIVAL_MS=");en_decimal(game_displayed_survival_ms());
 en_text(" PLAYER_HITS=");en_decimal(combat_displayed_player_hits());
 en_text(" FX_ENABLED=");en_decimal(effect_scene_selected());en_text(" FX_ACTIVE=");en_decimal(effect_scene_displayed_active());
 en_text(" FX_SPAWNED=");en_decimal(effect_scene_displayed_spawned());en_text(" FX_DROP_FULL=");en_decimal(effect_scene_displayed_dropped());
 en_text(" FX_EVICTED=");en_decimal(effect_scene_displayed_evicted());
 en_text(" GROUPS_ACTIVE=");en_decimal(bullet_scene_displayed_groups());
 en_text(" MEMBERS_ACTIVE=");en_decimal(bullet_scene_displayed_active());
 en_text(" MEMBERS_VISIBLE=");en_decimal(bullet_scene_displayed_visible());
 en_text(" PATTERN=");en_text(bullet_scene_pattern_mode()==3u?"RICH":(bullet_scene_pattern_mode()==2u?"MIXED":(bullet_scene_dual()?"DUAL":"SINGLE")));
 en_text(" GROUP_TARGET=");en_decimal(bullet_scene_stress());
 en_text(" BULLETS_FAN=");en_decimal(bullet_scene_displayed_fan());en_text(" STRESS=");en_decimal(bullet_scene_stress());
 en_text(" ENEMIES_ALIVE=");en_decimal(enemy_scene_alive_count());
 en_text(" HITS=");en_decimal(combat_displayed_hits());
 en_text(" KILLS=");en_decimal(combat_displayed_kills());
 en_text(" BOSS_HP=");en_decimal(enemy_scene_boss_hp());
 en_text(" COMBAT_US=");en_decimal(en_us(combat_displayed_ticks()));
 en_text(" COMBAT_MAX_US=");en_decimal(en_us(combat_displayed_max_ticks()));
 en_text(" PREP_US=");en_decimal(en_prep_us);en_text(" PREP_MAX_US=");en_decimal(en_prep_max_us);
 if(enemy_scene_selected()){
  u32 i;enemy_object_desc_t object;
  /* BOSS4 diagnostics are a bounded snapshot of confirmed coordinates.
   * Legacy X/Y/SPEED fields above continue to describe the SINGLE path. */
  for(i=0;i<4;i++)if(enemy_scene_displayed(i,&object)){
   en_text(" OBJ");en_decimal(i);en_text("=");en_decimal(object.word[0]&65535u);
   en_text(",");en_decimal(object.word[0]>>16);
  }
 }
 en_text("\r\n");
}
#endif /* !BUILD_VARIANT_GAME */
static void flow_phase(unsigned phase){
 flow.phase=phase;
 if(!uart_reply_begin(96))return;
 en_text("ENEMY FLOW PHASE=");en_text(flow_names[phase]);en_text(" ROUND=");en_decimal(flow.round);
 en_text(" SGEN=");en_decimal(en_display_generation);en_text("\r\n");uart_reply_commit();
}
#ifndef BUILD_VARIANT_GAME /* GHD: UART-only */
static void flow_status(void){
 en_text("ENEMY FLOW ABI=464C0100 MODE=");en_text(flow.enabled?"LIGHT_PRESSURE":"OFF");
 en_text(" PHASE=");en_text(flow_names[flow.phase]);en_text(" PAUSED=");en_decimal(flow.paused);
 en_text(" ROUND=");en_decimal(flow.round);en_text(" TRIGGERED=");en_decimal(flow.triggered);
 en_text(" PB_KILLS=");en_decimal(combat_displayed_pb_kills());en_text(" SGEN=");en_decimal(en_display_generation);
 en_text(" BLACK_EPOCH=");en_decimal(flow.black_epoch);en_text(" BLACK_GEN=");en_decimal(flow.black_generation);
 en_text(" PGEN=");en_decimal(flow.pressure_generation);en_text(" MOVING=");en_decimal(en_moving);en_text("\r\n");
}
#endif /* !BUILD_VARIANT_GAME */
static void flow_hold(u64 now){
 if(flow.phase==FL_BLACK_HOLD && !flow.paused){u64 dt=now-flow.last;flow.remaining=dt>=flow.remaining?0:flow.remaining-dt;}
 flow.last=now;
}
static int en_is_key(const char *s){return strncmp(s,"KEY",3)==0 && (s[3]==0 || s[3]==' ');}
static int en_is_background(const char *s){return strncmp(s,"BACKGROUND",10)==0 && (s[10]==0 || s[10]==' ');}
static gpu_result_t en_background_apply(u32 mode){
 gpu_result_t r=gpu_background_configure(mode);
 /* Old bit has only IMAGE. Never send it a new-mode write or accept GRADIENT. */
 r=r==GPU_ERR_UNSUPPORTED && mode==GPU_BACKGROUND_IMAGE?GPU_OK:r;
 /* CMD: the I4 source is the start menu only for the menu scene (WAVE0 + I4); every other scene restores the game I4 source. */
 return r==GPU_OK?gpu_background_source(mode==GPU_BACKGROUND_I4 && enemy_scene_selected()==ENEMY_SCENE_WAVE0):r;
}
/* CMD: LIGHT = the original light profile (WAVE8, IMAGE, MIXED, no decor). PRESSURE = the accepted WAVE40 target, restored explicitly:
 * WAVE40, RICH (lite/stagger off), pool 600 + quota, I4 game background, decor 2X, PIPE ON. Both: GAME ON, key off.
 * GHD: shield config OFF (no opening GUARD shield): a new game and a restart start with the shield off, KEY2 toggles it;
 * the pressure switch selects OFF too but only effect_scene_begin applies the config, so a shield that is on stays on. */
static gpu_result_t flow_profile(unsigned pressure){
 gpu_result_t r;
 if((asset_upload_valid_mask()&0x0ffffffeu)!=0x0ffffffeu)return GPU_ERR_PARAM;
 if(pressure && (asset_upload_valid_mask()&0xf0000000u)!=0xf0000000u)return GPU_ERR_PARAM;
 r=enemy_scene_select(pressure?ENEMY_SCENE_WAVE40:ENEMY_SCENE_WAVE8);if(r!=GPU_OK)return r;
 bullet_scene_legacy_period(pressure?100u:BULLET_LIGHT_PERIOD_PCT); /* GHD5: LIGHT density (before every LIGHT bullet_scene_begin) */
 if(player_scene_select(1)!=GPU_OK || bullet_scene_select(1)!=GPU_OK || player_bullet_scene_select(1)!=GPU_OK ||
    missile_scene_select(1)!=GPU_OK || effect_scene_select(1)!=GPU_OK || bullet_scene_pattern(pressure?3:2)!=GPU_OK ||
    decor_scene_select(pressure || (asset_upload_valid_mask()&0x60000000u)==0x60000000u)!=GPU_OK ||
    (decor_scene_selected() && decor_scene_rate(1)!=GPU_OK))return GPU_ERR_PARAM;
 decor_scene_edge_mode(!pressure); /* GHD4: light = edge glow shots (needs skins ID29/30), pressure = turret shots */
 if(pressure){bullet_scene_rich_options(0,0);bullet_scene_rich_pool(bullet_scene_density_pool(),1);en_pipe=1;} /* GHD4: tier pool cap */
 game_select(1);if(effect_shield_select(SHIELD_OFF)!=GPU_OK)return GPU_ERR_PARAM;
 bullet_scene_set_stress(0);en_background=pressure?GPU_BACKGROUND_I4:GPU_BACKGROUND_IMAGE;
 r=gpu_enemy_key_configure(0);if(r!=GPU_OK)return r;
 return en_background_apply(en_background);
}
#ifndef BUILD_VARIANT_GAME /* GHD: UART-only */
static void en_background_result(gpu_result_t r){
 en_text(r==GPU_ERR_BUSY?"ENEMY BACKGROUND BUSY\r\n":r==GPU_ERR_UNSUPPORTED?
  "ENEMY BACKGROUND UNSUPPORTED_HW\r\n":"ENEMY BACKGROUND ERROR READBACK\r\n");
}
static void en_background_command(const char *s,int peer_busy){
 u32 config=0,frame=0,error=0,mode;gpu_result_t result;
 if(peer_busy || renderer_cpu_sync_waiting()){en_text("ENEMY BACKGROUND BUSY\r\n");return;}
 if(strcmp(s,"BACKGROUND")==0){
  result=gpu_background_get(&config,&frame,&error);
  en_text("ENEMY BACKGROUND SUPPORTED=");en_decimal(result==GPU_OK);
  en_text(" CONFIG=");en_decimal(config);en_text(" FRAME_VALID=");en_decimal((frame&GPU_BACKGROUND_FRAME_VALID)!=0);
  en_text(" FRAME=");en_decimal(frame&1u);en_text(" ERROR=");en_decimal(error);
  /* Claim valid is not SHOWN. Software only publishes completed frames. */
  en_text(" SHOWN=");en_decimal(en_frames!=0 && enemy_test_active() && !en_exit && !en_error);
  en_text("\r\n");return;
 }
 if(enemy_test_active()){en_text("ENEMY BACKGROUND BUSY\r\n");return;}
 if(strcmp(s,"BACKGROUND IMAGE")==0)mode=GPU_BACKGROUND_IMAGE;
 else if(strcmp(s,"BACKGROUND GRADIENT")==0)mode=GPU_BACKGROUND_GRADIENT;
 else if(strcmp(s,"BACKGROUND I4")==0)mode=GPU_BACKGROUND_I4;
 else{en_text("ENEMY BACKGROUND ERROR PARAM\r\n");return;}
 result=en_background_apply(mode);
 if(result!=GPU_OK){en_background_result(result);return;}
 en_background=mode;en_text("ENEMY BACKGROUND OK CONFIG=");en_decimal(mode);en_text("\r\n");
}
static void en_key(const char *s,int peer_busy){
 u32 config=0,frame=0,key=0;unsigned i;gpu_result_t result;
 if(peer_busy || enemy_test_active() || renderer_cpu_sync_waiting()){
  en_text("ENEMY KEY BUSY\r\n");return;
 }
 if(strcmp(s,"KEY OFF")==0){
  result=gpu_enemy_key_get(&config,&frame);
  config &= 0xFFFFu; /* OFF retains the selected color; only reset clears it. */
 }else if(strncmp(s,"KEY ON ",7)==0 && strlen(s+7)==4){
  for(i=0;i<4;i++){
   unsigned c=(unsigned char)s[7+i],digit;
   if(c>='0' && c<='9')digit=c-'0';
   else if(c>='A' && c<='F')digit=c-'A'+10;
   else if(c>='a' && c<='f')digit=c-'a'+10;
   else {en_text("ENEMY KEY ERROR PARAM\r\n");return;}
   key=(key<<4)|digit;
  }
  config=GPU_ENEMY_KEY_ENABLE|key;result=GPU_OK;
 }else {en_text("ENEMY KEY ERROR PARAM\r\n");return;}
 if(result==GPU_OK)result=gpu_enemy_key_configure(config);
 if(result!=GPU_OK){
  en_text(result==GPU_ERR_BUSY?"ENEMY KEY BUSY\r\n":
          result==GPU_ERR_UNSUPPORTED?"ENEMY KEY UNSUPPORTED_HW\r\n":
          "ENEMY KEY ERROR READBACK\r\n");return;
 }
 en_text("ENEMY KEY OK CONFIG=");uart_reply_writeHex(BSP_UART_TERMINAL,(int)config);en_text("\r\n");
}
#endif /* !BUILD_VARIANT_GAME */
#ifndef BUILD_VARIANT_GAME /* GHD: UART-only */
static void en_perf(void){
 static const char *const names[]={"COMPOSE","BG_GRANT","BG_AR_WAIT","BG_AR_XFER",
  "BG_R_XFER","BG_R_WAIT","ASSET_GRANT","ASSET_AR_WAIT","ASSET_AR_XFER",
  "ASSET_R_XFER","ASSET_R_WAIT","FIFO_STALL","AW_STALL","W_STALL",
  "W_XFER","B_WAIT","B_XFER","INTERNAL","PRESENT"};
 uint32_t v[19],seq,flags,y,back,gen,after,abi=en_read(0x184);unsigned i,tries;
 /* 1.3 (V1 write overlap): W/B buckets count only while composing waits on the writer.
  * 1.4 (V1 area build): only COMPOSE [0] and PRESENT [18] are counted; buckets read 0. */
 if(abi!=0x45500100u && abi!=0x45500101u && abi!=0x45500102u && abi!=0x45500103u && abi!=0x45500104u){en_text("ENEMY PERF UNSUPPORTED_HW\r\n");return;}
 for(tries=0;tries<3;tries++){
  seq=en_read(0x188);flags=en_read(0x18c);
  for(i=0;i<19;i++)v[i]=en_read(0x190+4*i);
  y=en_read(0x1dc);back=en_read(0x1e0);gen=en_read(0x1e4);after=en_read(0x188);
  if(seq==after)break;
 }
 if(tries==3){en_text("ENEMY PERF RETRY\r\n");return;}
 en_text("ENEMY PERF HZ=100000000 SEQ=");en_decimal(seq);
 en_text(" FLAGS=");en_decimal(flags);en_text(" Y=");en_decimal(y);
 en_text(" BACK=");uart_reply_writeHex(BSP_UART_TERMINAL,(int)back);
 en_text(" GENERATION=");en_decimal(gen);en_text(" EVENTS=");uart_reply_writeHex(BSP_UART_TERMINAL,(int)gpu_get_async_event_status());
 en_text(" ABI=");uart_reply_writeHex(BSP_UART_TERMINAL,(int)abi);
 en_text(" CAP=");uart_reply_writeHex(BSP_UART_TERMINAL,(int)en_read(0x1e8));
 en_text(abi==0x45500104u?" BUCKETS=0":" BUCKETS=1");
 for(i=0;i<19;i++){en_text(" ");en_text(names[i]);en_text("=");en_decimal(v[i]);}
 en_text("\r\n");
}
static void en_intervals_status(void){
 uint32_t first=en_intervals>64?en_intervals-64:0,i;
 en_text("ENEMY INTERVALS TOTAL=");en_decimal(en_intervals);en_text(" FIRST=");en_decimal(first);
 en_text(" US=");
 for(i=first;i<en_intervals;i++){if(i!=first)en_text(",");en_decimal(en_interval_us[i&63u]);}
 en_text("\r\n");
}
/* S1: "RICH LITE|STAGGER ON|OFF" (idle only), and STRESS/GROUPS_STRESS refused while PATTERN RICH:
 * RICH has no refill, a stress target would bypass the per-plane periods. Returns 1 when handled. */
static __attribute__((noinline,optimize("Os"))) int en_rich_command(const char *line,int peer_busy){
 static const char *const opt[4]={"RICH LITE OFF","RICH LITE ON","RICH STAGGER OFF","RICH STAGGER ON"};
 const char *s=strncmp(line,"GROUPS_",7)?line:line+7;u32 k;
 if(bullet_scene_pattern_mode()==3u && !strncmp(s,"STRESS",6) && (!s[6] || s[6]==' ')){en_text("ENEMY STRESS ERROR RICH\r\n");return 1;}
 if(strncmp(line,"RICH ",5))return 0;
 if(strcmp(line,"RICH POOL")==0){
  en_text("ENEMY RICH POOL CAP=");en_decimal(bullet_scene_rich_pool_cap());en_text(" QUOTA=");en_decimal(bullet_scene_rich_quota());
  en_text(" SHARED_MIN=");en_decimal(RICH_QUOTA_SHARED_MIN);en_text(" FLOOR=");
  for(k=1;k<RICH_ROLE_COUNT;k++){if(k>1)en_text(",");en_decimal(bullet_scene_rich_floor(k));}
  en_text(" LIVE_AT_FIRE=");for(k=1;k<RICH_ROLE_COUNT;k++){if(k>1)en_text(",");en_decimal(bullet_scene_rich_live_role(k));}
  en_text(" DEFER_QUOTA=");en_decimal(bullet_scene_rich_stat(8));en_text(" DEFER_POOL=");en_decimal(bullet_scene_rich_stat(2));en_text("\r\n");return 1;
 }
 if(peer_busy || enemy_test_active() || renderer_cpu_sync_waiting()){en_text("ENEMY RICH BUSY\r\n");return 1;}
 if(strcmp(line,"RICH POOL 512")==0 || strcmp(line,"RICH POOL 600")==0){
  bullet_scene_rich_pool(line[10]=='5'?512u:600u,bullet_scene_rich_quota());
  en_text("ENEMY RICH POOL OK CAP=");en_decimal(bullet_scene_rich_pool_cap());en_text(" QUOTA=");en_decimal(bullet_scene_rich_quota());en_text("\r\n");return 1;
 }
 if(strcmp(line,"RICH QUOTA ON")==0 || strcmp(line,"RICH QUOTA OFF")==0){
  bullet_scene_rich_pool(bullet_scene_rich_pool_cap(),line[12]=='N');
  en_text("ENEMY RICH POOL OK CAP=");en_decimal(bullet_scene_rich_pool_cap());en_text(" QUOTA=");en_decimal(bullet_scene_rich_quota());en_text("\r\n");return 1;
 }
 for(k=0;k<4 && strcmp(line,opt[k]);k++);
 if(k==4){en_text("ENEMY RICH ERROR PARAM\r\n");return 1;}
 bullet_scene_rich_options(k<2?k:bullet_scene_rich_option(0),k<2?bullet_scene_rich_option(1):k-2u);
 en_text("ENEMY RICH OK LITE=");en_decimal(bullet_scene_rich_option(0));en_text(" STAGGER=");en_decimal(bullet_scene_rich_option(1));en_text("\r\n");return 1;
}
/* S1 RICH diagnostics (ABI 52490101), all from the last confirmed frame, counters since BEGIN/RESTART.
 * V = 33 comma-separated values in this fixed order:
 *  0 FRAME 1 SGEN 2 LITE 3 STAGGER 4 LITE_ACTIVE 5 LANE_GUARD 6 EVENTS 7 DEFER_BUDGET 8 DEFER_POOL 9 DEFER_RESERVE
 *  10 LATE 11 CEASE 12 MAX_WAIT 13 PEAK_GROUPS 14 GROUPS 15 VISIBLE 16 MS_BLUE_ENABLED 17 MS_BLUE_SUPPRESSED
 *  18 MS_BLUE_SPAWNED 19 MS_RED_SPAWNED 20 MS_BLUE_ACTIVE 21 MS_RED_ACTIVE 22 PATTERN(3=RICH)
 *  23..29 visible members by role 0 OTHER,1 FANX,2 TRIPLE,3 THREE,4 BLUE3,5 RING2,6 CURTAIN
 *  30..33 visible members by style 0 green,1 purple,2 blue,3 red */
static __attribute__((noinline,optimize("Os"))) void en_rich_status(void){
 u32 v[34],k;
 v[0]=en_frames;v[1]=en_display_generation;for(k=0;k<3;k++)v[2+k]=bullet_scene_rich_option(k);v[5]=enemy_scene_lane_guard();
 for(k=0;k<8;k++)v[6+k]=bullet_scene_rich_stat(k);
 v[14]=bullet_scene_displayed_groups();v[15]=bullet_scene_displayed_visible();v[16]=missile_scene_blue_enabled();v[17]=missile_scene_blue_suppressed();
 for(k=0;k<2;k++){v[18+k]=missile_scene_displayed_type(k,2);v[20+k]=missile_scene_displayed_type(k,0);}
 v[22]=bullet_scene_pattern_mode();for(k=0;k<RICH_ROLE_COUNT;k++)v[23+k]=bullet_scene_displayed_role_visible(k);
 for(k=0;k<BULLET_STYLE_COUNT;k++)v[30+k]=bullet_scene_displayed_style_visible(k);
 en_text("ENEMY RICH ABI=52490101 WAVE=");en_text(enemy_scene_name());en_text(" V=");
 for(k=0;k<34;k++){if(k)en_text(",");en_decimal(v[k]);}
 en_text("\r\n");
}
#endif /* !BUILD_VARIANT_GAME */
#ifndef BUILD_VARIANT_GAME
static int app_command(const char *s);
#endif
static int app_query(const char *s);static const char *app_name(void);
static int en_handle_line(const char *line,int peer_busy){
 while(*line==' ' || *line=='\t')++line;
 if(strncmp(line,"ENEMY ",6)!=0)return 0;
 line+=6;while(*line==' ')++line;
#ifndef BUILD_VARIANT_GAME
 if(strcmp(line,"APP")==0 || strncmp(line,"APP ",4)==0)return app_command(line);
#endif
#ifndef BUILD_VARIANT_GAME /* GHD: UART-only */
 if(app_owns_input() && !app_query(line)){en_text("ENEMY APP_BUSY STATE=");en_text(app_name());en_text("\r\n");return 1;}
 if(en_is_key(line)){en_key(line,peer_busy);return 1;}
 if(en_is_background(line)){en_background_command(line,peer_busy);return 1;}
 if(strncmp(line,"PLAYER BULLETS",14)==0 && (line[14]==0 || line[14]==' ')){
  u32 enable;gpu_result_t result;
  if(peer_busy || enemy_test_active() || renderer_cpu_sync_waiting()){en_text("ENEMY PLAYER BULLETS BUSY\r\n");return 1;}
  if(strcmp(line,"PLAYER BULLETS ON")==0)enable=1;
  else if(strcmp(line,"PLAYER BULLETS OFF")==0)enable=0;
  else{en_text("ENEMY PLAYER BULLETS ERROR PARAM\r\n");return 1;}
  result=player_bullet_scene_select(enable);
  if(result==GPU_ERR_UNSUPPORTED)en_text("ENEMY PLAYER BULLETS UNSUPPORTED_HW\r\n");
  else if(result!=GPU_OK)en_text("ENEMY PLAYER BULLETS ERROR BOSS4_PLAYER_REQUIRED\r\n");
  else en_text(enable?"ENEMY PLAYER BULLETS ON\r\n":"ENEMY PLAYER BULLETS OFF\r\n");
  return 1;
 }
 if(strncmp(line,"PLAYER",6)==0 && (line[6]==0 || line[6]==' ')){
  u32 enable;gpu_result_t result;
  if(peer_busy || enemy_test_active() || renderer_cpu_sync_waiting()){en_text("ENEMY PLAYER BUSY\r\n");return 1;}
  if(strcmp(line,"PLAYER ON")==0)enable=1;
  else if(strcmp(line,"PLAYER OFF")==0)enable=0;
  else{en_text("ENEMY PLAYER ERROR PARAM\r\n");return 1;}
  if(!enable && effect_shield_mode()){en_text("ENEMY PLAYER ERROR SHIELD_ACTIVE\r\n");return 1;}
  result=player_scene_select(enable);
  if(result==GPU_ERR_UNSUPPORTED)en_text("ENEMY PLAYER UNSUPPORTED_HW\r\n");
  else if(result!=GPU_OK)en_text("ENEMY PLAYER ERROR BOSS4_REQUIRED\r\n");
  else{if(!enable)(void)player_bullet_scene_select(0);en_text(enable?"ENEMY PLAYER ON\r\n":"ENEMY PLAYER OFF\r\n");}
  return 1;
 }
 if(strncmp(line,"BULLETS",7)==0 && (line[7]==0 || line[7]==' ')){
  u32 enable;gpu_result_t result;
  if(peer_busy || enemy_test_active() || renderer_cpu_sync_waiting()){en_text("ENEMY BULLETS BUSY\r\n");return 1;}
  if(strcmp(line,"BULLETS ON")==0)enable=1;
  else if(strcmp(line,"BULLETS OFF")==0)enable=0;
  else{en_text("ENEMY BULLETS ERROR PARAM\r\n");return 1;}
  result=bullet_scene_select(enable);
  if(result==GPU_ERR_UNSUPPORTED)en_text("ENEMY BULLETS UNSUPPORTED_HW\r\n");
  else if(result!=GPU_OK)en_text("ENEMY BULLETS ERROR BOSS4_REQUIRED\r\n");
  else en_text(enable?"ENEMY BULLETS ON\r\n":"ENEMY BULLETS OFF\r\n");
  return 1;
 }
 if(strncmp(line,"SCENE",5)==0 && (line[5]==0 || line[5]==' ')){
  u32 scene;gpu_result_t result;
  if(peer_busy || enemy_test_active() || renderer_cpu_sync_waiting()){en_text("ENEMY SCENE BUSY\r\n");return 1;}
  if(strcmp(line,"SCENE SINGLE")==0)scene=ENEMY_SCENE_SINGLE;
  else if(strcmp(line,"SCENE BOSS4")==0)scene=ENEMY_SCENE_BOSS4;
  else if(strcmp(line,"SCENE WAVE8")==0)scene=ENEMY_SCENE_WAVE8;
  else if(strcmp(line,"SCENE WAVE16")==0)scene=ENEMY_SCENE_WAVE16;
  else if(strcmp(line,"SCENE WAVE35")==0)scene=ENEMY_SCENE_WAVE35;
  else if(strcmp(line,"SCENE WAVE20")==0)scene=ENEMY_SCENE_WAVE20;
  else if(strcmp(line,"SCENE WAVE24")==0)scene=ENEMY_SCENE_WAVE24;
  else if(strcmp(line,"SCENE WAVE32")==0)scene=ENEMY_SCENE_WAVE32;
  else if(strcmp(line,"SCENE WAVE40")==0)scene=ENEMY_SCENE_WAVE40;
  else if(strcmp(line,"SCENE WAVE0")==0)scene=ENEMY_SCENE_WAVE0;
  else{en_text("ENEMY SCENE ERROR PARAM\r\n");return 1;}
  if(!scene && effect_shield_mode()){en_text("ENEMY SCENE ERROR SHIELD_ACTIVE\r\n");return 1;}
  result=enemy_scene_select(scene);
  if(result==GPU_ERR_UNSUPPORTED)en_text("ENEMY SCENE UNSUPPORTED_HW\r\n");
  else if(result!=GPU_OK)en_text("ENEMY SCENE ERROR CONFIG\r\n");
  else{if(!scene){(void)missile_scene_select(0);(void)bullet_scene_select(0);(void)player_bullet_scene_select(0);(void)player_scene_select(0);(void)effect_scene_select(0);game_select(0);}
   en_text("ENEMY SCENE ");en_text(enemy_scene_name());en_text("\r\n");}
  return 1;
 }
 if(strcmp(line,"EFFECTS")==0){
  const effect_snapshot_t *v=effect_scene_snapshot();
  en_text("ENEMY EFFECTS ABI=46580101 SESSION=");en_decimal(v->session);
  en_text(" SEQ=");en_decimal(v->sequence);en_text(" FRAME=");en_decimal(en_frames);
  en_text(" SHIELD_CONFIG=");en_decimal(effect_shield_mode());en_text(" MODE=");en_decimal(v->flags&255u);
  en_text(" CHARGES=");en_decimal((v->flags>>8)&255u);en_text(" ACTIVE=");en_decimal((v->flags>>16)&1u);
  en_text(" FLASH=");en_decimal((v->flags>>17)&1u);en_text(" REMAIN_MS=");en_decimal(en_us(v->remaining)/1000u);
  en_text(" ABSORBED=");en_decimal(v->absorbed);en_text(" SMALL=");en_decimal(v->counts&255u);
  en_text(" LARGE=");en_decimal((v->counts>>8)&255u);en_text(" SPARK=");en_decimal(v->counts>>16);
  en_text(" MERGED=");en_decimal(v->merged);en_text(" DROPPED=");en_decimal(effect_scene_displayed_dropped());en_text("\r\n");return 1;
 }
 if(strncmp(line,"SHIELD",6)==0 && (line[6]==0 || line[6]==' ')){
  u32 mode;gpu_result_t r;
  if(peer_busy || enemy_test_active() || renderer_cpu_sync_waiting()){en_text("ENEMY SHIELD BUSY\r\n");return 1;}
  if(strcmp(line,"SHIELD OFF")==0)mode=SHIELD_OFF;
  else if(strcmp(line,"SHIELD VISUAL")==0)mode=SHIELD_VISUAL;
  else if(strcmp(line,"SHIELD GUARD")==0)mode=SHIELD_GUARD;
  else{en_text("ENEMY SHIELD ERROR PARAM\r\n");return 1;}
  r=effect_shield_select(mode);en_text(r==GPU_OK?"ENEMY SHIELD OK\r\n":"ENEMY SHIELD ERROR REQUIRES_FX_PLAYER_GAME\r\n");return 1;
 }
 if(strncmp(line,"MISSILES",8)==0 && (line[8]==0 || line[8]==' ')){
  u32 enable;gpu_result_t r;
  if(strcmp(line,"MISSILES STATUS")==0){
   en_text("ENEMY MISSILES STATUS FRAME=");en_decimal(en_frames);en_text(" SGEN=");en_decimal(en_display_generation);
   en_text(" ENABLED=");en_decimal(missile_scene_selected());
   en_text(" MISSILES_ACTIVE=");en_decimal(missile_scene_displayed_active());en_text(" MISSILES_VISIBLE=");en_decimal(missile_scene_displayed_visible());
   en_text(" MISSILES_SPAWNED=");en_decimal(missile_scene_displayed_spawned());en_text(" MISSILES_DROPPED=");en_decimal(missile_scene_displayed_dropped());
   en_text(" BLUE_ACTIVE=");en_decimal(missile_scene_displayed_type(0,0));en_text(" BLUE_VISIBLE=");en_decimal(missile_scene_displayed_type(0,1));
   en_text(" RED_ACTIVE=");en_decimal(missile_scene_displayed_type(1,0));en_text(" RED_VISIBLE=");en_decimal(missile_scene_displayed_type(1,1));
   en_text("\r\n");return 1;
  }
  if(peer_busy || enemy_test_active() || renderer_cpu_sync_waiting()){en_text("ENEMY MISSILES BUSY\r\n");return 1;}
  if(strcmp(line,"MISSILES ON")==0)enable=1;else if(strcmp(line,"MISSILES OFF")==0)enable=0;
  else{en_text("ENEMY MISSILES ERROR PARAM\r\n");return 1;}
  r=missile_scene_select(enable);en_text(r==GPU_OK?(enable?"ENEMY MISSILES ON\r\n":"ENEMY MISSILES OFF\r\n"):"ENEMY MISSILES UNSUPPORTED_HW\r\n");return 1;
 }
 if(strncmp(line,"DECOR",5)==0 && (line[5]==0 || line[5]==' ')){
  gpu_result_t r;
  if(strcmp(line,"DECOR")==0){decor_scene_status(en_text,en_decimal);return 1;}
  if(peer_busy || enemy_test_active() || renderer_cpu_sync_waiting()){en_text("ENEMY DECOR BUSY\r\n");return 1;}
  if(strcmp(line,"DECOR RATE 1X")==0 || strcmp(line,"DECOR RATE 2X")==0){r=decor_scene_rate(line[11]=='2');
   en_text(r==GPU_OK?(decor_scene_rate_2x()?"ENEMY DECOR RATE 2X\r\n":"ENEMY DECOR RATE 1X\r\n"):"ENEMY DECOR RATE UNSUPPORTED_HW\r\n");return 1;}
  if(strcmp(line,"DECOR ON")==0)r=decor_scene_select(1);else if(strcmp(line,"DECOR OFF")==0)r=decor_scene_select(0);
  else{en_text("ENEMY DECOR ERROR PARAM\r\n");return 1;}
  en_text(r==GPU_OK?(decor_scene_selected()?"ENEMY DECOR ON\r\n":"ENEMY DECOR OFF\r\n"):"ENEMY DECOR UNSUPPORTED_HW\r\n");return 1;
 }
 if(strncmp(line,"PIPE",4)==0 && (line[4]==0 || line[4]==' ')){
  if(strcmp(line,"PIPE")==0){
   en_text("ENEMY PIPE ABI=50490101 PIPE=");en_decimal(en_pipe);en_text(" ACTIVE=");en_decimal(enemy_test_active() && en_pipe_on());
   en_text(" READY=");en_decimal(en_pipe_ready);en_text(" PREPS=");en_decimal(en_pipe_preps);en_text(" FRAMES=");en_decimal(en_pipe_frames);
   en_text(" DISCARDS=");en_decimal(en_pipe_discards);en_text(" PREP_MAX_US=");en_decimal(en_pipe_prep_max_us);en_text(" PREP_SUM_US=");en_decimal(en_pipe_prep_sum_us);en_text("\r\n");return 1;
  }
  if(peer_busy || enemy_test_active() || renderer_cpu_sync_waiting()){en_text("ENEMY PIPE BUSY\r\n");return 1;}
  if(strcmp(line,"PIPE ON")==0)en_pipe=1;else if(strcmp(line,"PIPE OFF")==0)en_pipe=0;
  else{en_text("ENEMY PIPE ERROR PARAM\r\n");return 1;}
  en_text(en_pipe?"ENEMY PIPE ON\r\n":"ENEMY PIPE OFF\r\n");return 1;
 }
 if(strncmp(line,"FX",2)==0 && (line[2]==0 || line[2]==' ')){
  u32 enable;gpu_result_t result;
  if(peer_busy || enemy_test_active() || renderer_cpu_sync_waiting()){en_text("ENEMY FX BUSY\r\n");return 1;}
  if(strcmp(line,"FX ON")==0)enable=1;else if(strcmp(line,"FX OFF")==0)enable=0;
  else{en_text("ENEMY FX ERROR PARAM\r\n");return 1;}
  result=effect_scene_select(enable);
  if(result==GPU_ERR_UNSUPPORTED)en_text("ENEMY FX UNSUPPORTED_HW\r\n");
  else if(result!=GPU_OK)en_text("ENEMY FX ERROR MULTI_REQUIRED\r\n");
  else en_text(enable?"ENEMY FX ON\r\n":"ENEMY FX OFF\r\n");
  return 1;
 }
 if(strncmp(line,"GAME",4)==0 && (line[4]==0 || line[4]==' ')){
  if(peer_busy || enemy_test_active() || renderer_cpu_sync_waiting()){en_text("ENEMY GAME BUSY\r\n");return 1;}
  if(strcmp(line,"GAME ON")==0){if(!player_scene_selected()){en_text("ENEMY GAME ERROR PLAYER_REQUIRED\r\n");return 1;}game_select(1);en_text("ENEMY GAME ON\r\n");}
  else if(strcmp(line,"GAME OFF")==0){if(effect_shield_mode()==SHIELD_GUARD){en_text("ENEMY GAME ERROR SHIELD_GUARD\r\n");return 1;}game_select(0);en_text("ENEMY GAME OFF\r\n");}
  else en_text("ENEMY GAME ERROR PARAM\r\n");
  return 1;
 }
 if(strncmp(line,"PATTERN",7)==0 && (line[7]==0 || line[7]==' ')){
  gpu_result_t r;u32 dual;
  if(peer_busy || enemy_test_active() || renderer_cpu_sync_waiting()){en_text("ENEMY PATTERN BUSY\r\n");return 1;}
  if(strcmp(line,"PATTERN SINGLE")==0)dual=0;else if(strcmp(line,"PATTERN DUAL")==0)dual=1;else if(strcmp(line,"PATTERN MIXED")==0)dual=2;else if(strcmp(line,"PATTERN RICH")==0)dual=3;
  else{en_text("ENEMY PATTERN ERROR PARAM\r\n");return 1;}
  r=bullet_scene_pattern(dual);en_text(r==GPU_OK?"ENEMY PATTERN OK\r\n":"ENEMY PATTERN UNSUPPORTED_HW\r\n");return 1;
 }
 if(en_rich_command(line,peer_busy))return 1;
 if(strncmp(line,"GROUPS_STRESS",13)==0 && (line[13]==0 || line[13]==' ')){
  if(!bullet_scene_dual()){en_text("ENEMY STRESS ERROR PATTERN\r\n");return 1;}
  line+=7; /* common parser sees STRESS; target explicitly counts groups */
 }else if(strncmp(line,"STRESS",6)==0 && bullet_scene_dual()){
  en_text("ENEMY STRESS ERROR USE_GROUPS_STRESS\r\n");return 1;
 }
 if(strncmp(line,"STRESS",6)==0 && (line[6]==0 || line[6]==' ')){
  u32 n=0;const char *q=line+6;
  if(peer_busy || enemy_test_active() || renderer_cpu_sync_waiting()){en_text("ENEMY STRESS BUSY\r\n");return 1;}
  while(*q==' ')q++;
  if(strcmp(q,"OFF")==0)n=0;
  else{if(!*q){en_text("ENEMY STRESS ERROR PARAM\r\n");return 1;}
   while(*q>='0' && *q<='9' && n<=BULLET_CAPACITY){n=n*10u+(u32)(*q-'0');q++;}
   if(*q || n>BULLET_CAPACITY){en_text("ENEMY STRESS ERROR PARAM\r\n");return 1;}}
  bullet_scene_set_stress(n);en_text("ENEMY STRESS ");en_decimal(n);en_text("\r\n");
  return 1;
 }
 if(strncmp(line,"FLOW",4)==0 && (line[4]==0 || line[4]==' ')){
  if(!line[4]){flow_status();return 1;}
  if(peer_busy || enemy_test_active() || renderer_cpu_sync_waiting() || !game_lifecycle_safe()){en_text("ENEMY FLOW BUSY\r\n");return 1;}
  if(strcmp(line,"FLOW OFF")==0)flow.enabled=0;
  else if(strcmp(line,"FLOW LIGHT_PRESSURE")==0)flow.enabled=1;
  else{en_text("ENEMY FLOW ERROR PARAM\r\n");return 1;}
  flow.phase=FL_OFF;en_text("ENEMY FLOW OK MODE=");en_text(flow.enabled?"LIGHT_PRESSURE":"OFF");en_text("\r\n");return 1;
 }
#endif /* !BUILD_VARIANT_GAME */
 if(strcmp(line,"RESTART")==0){
  /* Accepted after game over in any frame state; applied at the next SHOWN
   * boundary (owner drained), reseeding the round in place. */
  if(!enemy_test_active() || !game_over() || (flow.enabled && !game_displayed_over()) || en_exit || en_error ||
     (en_state!=EN_CLAIM && en_state!=EN_DRAWING && en_state!=EN_SHOWN && en_state!=EN_RECYCLE)){en_text("ENEMY ERROR RESTART_NOT_READY\r\n");return 1;}
  en_restart_pending=1;player_input_revoke(clint_getTime(BSP_CLINT));en_text("ENEMY RESTART ACCEPTED\r\n");return 1;
 }
#ifndef BUILD_VARIANT_GAME /* GHD: UART-only */
 if(strcmp(line,"STATUS")==0){en_status();return 1;}
 if(strcmp(line,"STATS")==0 || strcmp(line,"STATS RESET")==0){en_stats(line[5]!=0);return 1;}
 if(strncmp(line,"FRAMES",6)==0 && (line[6]==0 || line[6]==' ')){en_frames_query(line+6);return 1;}
 if(strcmp(line,"PERF")==0){en_perf();return 1;}
 if(strcmp(line,"BUILD")==0){en_text("ENEMY BUILD ID=" BUILD_ID " BU=");uart_reply_writeHex(BSP_UART_TERMINAL,(int)en_read(0x240));
  en_text(" CAP=");uart_reply_writeHex(BSP_UART_TERMINAL,(int)en_read(0x1e8));en_text(" BCAP=");en_decimal(en_read(0x244));
  en_text(" HUD=");uart_reply_writeHex(BSP_UART_TERMINAL,(int)read_u32(GPU_REG_GAME_HUD_ABI));en_text(" CLINT_HZ=");en_decimal(BSP_CLINT_HZ);en_text("\r\n");return 1;}
 if(strcmp(line,"CPUCAL")==0){
  u64 t0,t1;u32 n=1000000u,a=0;
  if(peer_busy || enemy_test_active() || renderer_cpu_sync_waiting()){en_text("ENEMY CPUCAL BUSY\r\n");return 1;}
  t0=clint_getTime(BSP_CLINT);
#ifdef __riscv
  __asm__ volatile("1: addi %0,%0,-1\n add %1,%1,%0\n bnez %0,1b":"+r"(n),"+r"(a));
#else
  while(n){n--;a+=n;} /* host C/MMIO model only */
#endif
  t1=clint_getTime(BSP_CLINT);
  en_text("ENEMY CPUCAL ITER=1000000 TICKS=");en_decimal(t1-t0);en_text(" CLINT_HZ=");en_decimal(BSP_CLINT_HZ);
  en_text(" SUM=");uart_reply_writeHex(BSP_UART_TERMINAL,(int)a);en_text("\r\n");return 1;}
 if(strcmp(line,"INTERVALS")==0){en_intervals_status();return 1;}
 if(strcmp(line,"RICH")==0){en_rich_status();return 1;}
#endif /* !BUILD_VARIANT_GAME */
 if(strcmp(line,"BEGIN")==0 || strcmp(line,"MOVE")==0){
  unsigned move=strcmp(line,"MOVE")==0;uint32_t abi=en_read(ENEMY_REG_ABI);
  if(move && abi!=ENEMY_ABI){en_text("ENEMY ERROR UNSUPPORTED_MOVE_HW\r\n");return 1;}
  if(enemy_test_active()){
   if(move && !en_moving){
    if(en_stop_pending){en_text("ENEMY ERROR STOP_PENDING\r\n");return 1;}
    if(en_exit || en_error || en_state==EN_DRAINING || en_state==EN_RESTORING ||
       en_state==EN_RESTORE_FAILED){en_text("ENEMY ERROR BUSY\r\n");return 1;}
    flow.paused=0;flow.last=en_since=clint_getTime(BSP_CLINT);
    en_move_start();en_text("ENEMY MOVING\r\n");
   }
#ifndef BUILD_VARIANT_GAME
   else en_status();
#endif
   return 1;
  }
  if(peer_busy){en_text("ENEMY ERROR BUSY\r\n");return 1;}
  if(flow.enabled){
   if(renderer_cpu_sync_waiting() || !game_lifecycle_safe()){en_text("ENEMY FLOW BUSY\r\n");return 1;}
   if(flow_profile(0)!=GPU_OK){en_text("ENEMY ERROR FLOW_PROFILE\r\n");return 1;}
  }
  /* ELF reload does not reset PL. Confirm actual mode before exempting ID1. */
  {gpu_result_t r;
   if(renderer_cpu_sync_waiting()){
    /* Writer callback may register intent, but must never configure hardware.
     * A matching readback is enough; mismatches retry from the main loop. */
    u32 c=0,f=0,e=0;r=gpu_background_get(&c,&f,&e);
    if(r==GPU_ERR_UNSUPPORTED && en_background==GPU_BACKGROUND_IMAGE)r=GPU_OK;
    else if(r==GPU_OK && (c!=en_background || (en_read(ENEMY_REG_STATUS)&0x81u)!=0x80u))r=GPU_ERR_BUSY;
    else if(r==GPU_OK && en_read(0x3D0u)==16u && en_read(0x324u)!=(u32)(en_background==GPU_BACKGROUND_I4 && enemy_scene_selected()==ENEMY_SCENE_WAVE0))r=GPU_ERR_BUSY;
   }else r=en_background_apply(en_background);
#ifndef BUILD_VARIANT_GAME
   if(r!=GPU_OK){en_background_result(r);return 1;}}
#else
   if(r!=GPU_OK)return 1;}
#endif
  if(bullet_scene_selected() && (!asset_upload_bullet_mask(0) || !asset_upload_bullet_mask(1))){en_text("ENEMY ERROR BULLET_MASK_METADATA_REUPLOAD_11_13\r\n");return 1;}
  if(player_scene_selected() && asset_upload_player_mask()<0){en_text("ENEMY ERROR PLAYER_MASK_METADATA_REUPLOAD_15\r\n");return 1;}
  if(player_bullet_scene_selected() && !asset_upload_bullet_mask(1)){en_text("ENEMY ERROR PLAYER_BULLET_MASK_METADATA_REUPLOAD_13\r\n");return 1;}
  if(decor_scene_selected() && !decor_scene_hw_supported()){en_text("ENEMY ERROR DECOR_UNSUPPORTED_HW\r\n");return 1;}
  if(missile_scene_selected()){
   u32 cap;if(gpu_missile_scene_capacity(&cap)!=GPU_OK || !asset_upload_missile_mask(0) || !asset_upload_missile_mask(1)){en_text("ENEMY ERROR MISSILE_ABI_OR_MASK_REUPLOAD_25_27\r\n");return 1;}
  }
  {u32 required=(missile_scene_selected()?MISSILE_REQUIRED_ASSETS:0u)|enemy_scene_required_assets()|(bullet_scene_selected()?BULLET_REQUIRED_ASSETS:0u)|(player_scene_selected()?PLAYER_REQUIRED_ASSETS:0u)|(player_bullet_scene_selected()?PLAYER_BULLET_REQUIRED_ASSETS:0u)|(effect_scene_selected()?EFFECT_REQUIRED_ASSETS:0u);
   if(en_background==GPU_BACKGROUND_GRADIENT || en_background==GPU_BACKGROUND_I4)required &= ~(1u<<1);
   if(en_background==GPU_BACKGROUND_I4)required |= BG_I4_REQUIRED_ASSETS;
   required |= decor_scene_required_assets();
   if((asset_upload_valid_mask()&required)!=required){en_text("ENEMY ERROR ASSETS_INVALID\r\n");return 1;}
   if(enemy_scene_selected()==ENEMY_SCENE_WAVE0 && (asset_upload_valid_mask_hi()&MENU_REQUIRED_ASSETS_HI)!=MENU_REQUIRED_ASSETS_HI){en_text("ENEMY ERROR ASSETS_INVALID MENU_32_33\r\n");return 1;}}
  if(abi!=ENEMY_ABI && abi!=ENEMY_STATIC_ABI){en_text("ENEMY ERROR UNSUPPORTED_HW\r\n");return 1;}
  if(!en_hooks.snapshot || !en_hooks.prepare || !en_hooks.restore || !en_hooks.restored){en_text("ENEMY ERROR HOOKS\r\n");return 1;}
  if(flow.enabled){flow.round++;flow.triggered=flow.restarting=0;flow.paused=!move;flow.black_epoch=flow.black_generation=flow.pressure_generation=0;flow_phase(FL_LIGHT);}
  en_hooks.snapshot(&en_mode,&en_source);en_error=en_exit=en_started=en_restore_registered=0;
  en_moving=en_stop_pending=en_y=en_frames=en_run_frames=0;en_frame_motion=move;
  en_intervals=en_gap25=en_interval_min=en_interval_max=0;
  en_fraction=en_frame_ticks=en_comp_ticks=en_present_ticks=0;en_split_seen=en_last_split=en_run_armed=0;
  en_prep_us=en_prep_max_us=en_prep_open=0;en_diag_reset();
  en_motion_time=en_run_start=en_run_end=clint_getTime(BSP_CLINT);
  if(enemy_scene_selected()!=ENEMY_SCENE_WAVE0)game_hud_begin(); /* CMD: no HUD over the start menu */
  if(move)en_move_start();
  en_enter(EN_PREPARING);en_text("ENEMY PREPARING\r\n");return 1;
 }
 if(strcmp(line,"STOP")==0){
  if(flow.enabled){flow_hold(clint_getTime(BSP_CLINT));flow.paused=1;}
  player_input_revoke(clint_getTime(BSP_CLINT));
  if(en_exit){en_text("ENEMY ERROR EXIT_PENDING\r\n");return 1;}
  if(en_error){en_text("ENEMY ERROR FAILED\r\n");return 1;}
  if(!en_moving && !en_stop_pending && (!enemy_test_active() || en_state==EN_SHOWN)){
   en_text("ENEMY STOPPED\r\n");return 1;
  }
  en_moving=0;en_motion_time=clint_getTime(BSP_CLINT);
  en_stop_pending=1;
  if(!enemy_test_active() || (en_state==EN_SHOWN && !en_pipe_ready && (!flow.enabled || flow.phase==FL_LIGHT || flow.phase==FL_PRESSURE || flow.phase==FL_BLACK_HOLD) && !renderer_cpu_sync_waiting()))en_stop_done();
  else en_text("ENEMY STOP ACCEPTED\r\n");
  return 1;
 }
 if(strcmp(line,"EXIT")==0){
  if(flow.enabled)flow.paused=1;
  player_input_revoke(clint_getTime(BSP_CLINT));
  if(!enemy_test_active()){en_text("ENEMY EXITED\r\n");return 1;}
  en_exit=1;en_moving=0;en_stop_pending=0;
  if(en_state==EN_RESTORE_FAILED){en_error=0;en_restore_registered=0;en_enter(EN_DRAINING);}
  en_text("ENEMY EXIT ACCEPTED\r\n");return 1;
 }
 en_text("ENEMY ERROR COMMAND\r\n");return 1;
}
#ifndef BUILD_VARIANT_GAME /* GHD: UART-only */
static __attribute__((noinline)) void en_joystick(const char *q,u64 now,int busy){
   u32 v[3];unsigned k=0;int ok=1;
   if(strcmp(q,"STATUS")==0){
    const joystick_status_t *s=joystick_status();
    en_text("PLAYER JOY ABI=3 WORDS=");
    for(k=0;k<sizeof(*s)/4;k++){u32 word;memcpy(&word,(const char*)s+4*k,4);if(k)en_text(" ");en_decimal(word);}en_text("\r\n");
   }else if(strcmp(q,"DIAG")==0){
    const joystick_diag_t *d=joystick_diag();
    en_text("PLAYER JOY DIAG ABI=1 WORDS=");
    for(k=0;k<sizeof(*d)/4;k++){u32 word;memcpy(&word,(const char*)d+4*k,4);if(k)en_text(" ");en_decimal(word);}en_text("\r\n");
   }else if(busy || en_moving || en_stop_pending || renderer_cpu_sync_waiting() || (enemy_test_active() && en_state!=EN_SHOWN))en_text("PLAYER JOY BUSY\r\n");
   else{
    if(strcmp(q,"OFF")==0)joystick_enable(now,0);
    else if(strcmp(q,"RAW")==0)joystick_enable(now,1);
    else if(strcmp(q,"RETRY")==0)joystick_retry(now);
    else if(strncmp(q,"CENTER ",7)==0){
     q+=7;for(k=0;k<3;k++){v[k]=0;if(*q<'0' || *q>'9'){ok=0;break;}while(*q>='0' && *q<='9' && v[k]<=32767)v[k]=v[k]*10u+*q++-'0';if(k<2){if(*q++!=' '){ok=0;break;}}}
     if(*q)ok=0;if(ok)ok=joystick_center(now,v);
    }else ok=0;
    en_text(ok?"PLAYER JOY OK\r\n":"PLAYER JOY ERROR\r\n");
   }
}
#endif /* !BUILD_VARIANT_GAME */
#ifdef BUILD_VARIANT_GAME
/* GHD game ELF: only the APP issues commands (BEGIN/MOVE/RESTART/EXIT), no text output. */
static int en_dispatch_line(const char *line,int peer_busy,int protocol_output){(void)protocol_output;return en_handle_line(line,peer_busy);}
int enemy_test_handle_line(const char *line,int peer_busy){return en_handle_line(line,peer_busy);}
int enemy_test_protocol_reply_pending(void){return 0;}
int enemy_test_service_protocol_reply(void){return 0;}
#else
static int en_dispatch_line(const char *line,int peer_busy,int protocol_output){
 int result;const char *argument;unsigned key_command;
 while(*line==' ' || *line=='\t')++line;
 if(strncmp(line,"PLAYER",6)==0 && (line[6]==0 || line[6]==' ')){
  u64 now=clint_getTime(BSP_CLINT);
  if(player_input_is_data(line)){
   player_input_service(now,en_input_moving());
   if(!peer_busy && !protocol_output && en_input_moving() && !app_owns_input())player_input_line(line,now);
   return 1;
  }
  if(!uart_reply_begin(UART_REPLY_QUERY_MAX))return -1;
  if(protocol_output)en_key_protocol_reply=1;
  if(strcmp(line,"PLAYER CAPS")==0 && !protocol_output){
   u32 cap;en_text(gpu_player_bullet_scene_capacity(&cap)==GPU_OK?"PLAYER CAPS ABI=50490100 FIRE=1\r\n":"PLAYER CAPS ABI=50490100 FIRE=0\r\n");
  }
  else if(strncmp(line,"PLAYER JOY ",11)==0){
   en_joystick(line+11,now,peer_busy || protocol_output);
  }else if(strcmp(line,"PLAYER SHIELD")==0){
   if(peer_busy || protocol_output || app_owns_input() || !en_input_moving() || !game_running())en_text("PLAYER SHIELD BUSY\r\n");
   else{game_keys_request_shield();en_text("PLAYER SHIELD ACCEPTED\r\n");}
  }else if(strcmp(line,"PLAYER CONNECT")==0){
   if(peer_busy || protocol_output || app_owns_input() || !en_input_moving())en_text("PLAYER ERROR BUSY\r\n");
   else{u32 session=player_input_connect(now);en_text("PLAYER CONNECTED ");uart_reply_writeHex(BSP_UART_TERMINAL,(int)session);en_text("\r\n");}
  }else en_text(protocol_output?"PLAYER ERROR BUSY\r\n":"PLAYER ERROR COMMAND\r\n");
  uart_reply_commit();return 1;
 }
 if(strncmp(line,"ENEMY ",6)!=0)return 0;
 argument=line+6;while(*argument==' ')++argument;key_command=(strncmp(argument,"FLOW",4)==0) || (strncmp(argument,"MISSILES",8)==0) || (strncmp(argument,"DECOR",5)==0) || (strncmp(argument,"PIPE",4)==0) || (strncmp(argument,"PATTERN",7)==0) || (strncmp(argument,"GROUPS_STRESS",13)==0) || en_is_key(argument) || en_is_background(argument) || (strncmp(argument,"BULLETS",7)==0 && (argument[7]==0 || argument[7]==' ')) || (strncmp(argument,"SCENE",5)==0 && (argument[5]==0 || argument[5]==' ')) || (strncmp(argument,"PLAYER",6)==0 && (argument[6]==0 || argument[6]==' ')) ||
  (strncmp(argument,"SHIELD",6)==0 && (argument[6]==0 || argument[6]==' ')) || (strncmp(argument,"RICH ",5)==0) || (strcmp(argument,"EFFECTS")==0) || (strncmp(argument,"FX",2)==0 && (argument[2]==0 || argument[2]==' ')) || (strncmp(argument,"GAME",4)==0 && (argument[4]==0 || argument[4]==' ')) || (strncmp(argument,"STRESS",6)==0 && (argument[6]==0 || argument[6]==' '));
 /* Protocol sessions exclude enemy motion and retain their original UART
  * reply/ACK ordering. Normal commands capture one immutable text response. */
 en_protocol_text=protocol_output && !enemy_test_active() && !key_command;
 if(!en_protocol_text && !uart_reply_begin(UART_REPLY_QUERY_MAX))return -1;
 if(protocol_output && key_command)en_key_protocol_reply=1;
 result=en_handle_line(line,peer_busy || (protocol_output && key_command));
 if(!en_protocol_text)uart_reply_commit();
 en_protocol_text=0;return result;
}
int enemy_test_handle_line(const char *line,int peer_busy){
 return en_dispatch_line(line,peer_busy,0);
}
int enemy_test_handle_protocol_line(const char *line,int peer_busy){
 return en_dispatch_line(line,peer_busy,1);
}
int enemy_test_protocol_reply_pending(void){return en_key_protocol_reply!=0;}
int enemy_test_service_protocol_reply(void){
 if(!en_key_protocol_reply)return 0;
 uart_reply_service();
 if(!uart_reply_pending())en_key_protocol_reply=0;
 return 1;
}
/* Returns 1 while the flow owns a transition. UART callbacks never call it. */
#endif
static int flow_service(void){
 u32 s;int r;u64 now=clint_getTime(BSP_CLINT);
 if(!flow.enabled || flow.phase<FL_DRAIN || flow.phase==FL_PRESSURE || flow.phase==FL_FAILED || en_exit || en_error)return 0;
 if(flow.phase==FL_PRESSURE_FIRST)return 0;
 if((flow.phase==FL_DRAIN || flow.phase==FL_BLACK_WAIT || (flow.phase==FL_PRESSURE_PREP && !flow.paused)) && en_timeout()){en_error=8;en_moving=0;flow_phase(FL_FAILED);en_enter(EN_FAILED);return 1;}
 if(flow.phase==FL_DRAIN){
  s=en_read(ENEMY_REG_STATUS);if(s&1u){en_control(2);return 1;}if(!(s&128u))return 1;
  enemy_scene_cancel();bullet_scene_cancel();missile_scene_cancel();player_bullet_scene_cancel();effect_scene_cancel();
  if(missile_scene_disable()!=GPU_OK || effect_scene_disable()!=GPU_OK || player_bullet_scene_disable()!=GPU_OK || bullet_scene_disable()!=GPU_OK || player_scene_disable()!=GPU_OK){en_error=7;return 1;}
  game_hud_end();
  if(game_request_idle(0,2)<0){en_error=8;return 1;}
  flow.black_epoch=game_idle_epoch();flow_phase(FL_BLACK_WAIT);en_since=now;return 1;
 }
 if(flow.phase==FL_BLACK_WAIT){
  r=game_idle_confirmed();if(r<0){en_error=8;return 1;}
  s=game_idle_generation(flow.black_epoch);
  if(!r || s==flow.trigger_generation || game_idle_epoch()!=flow.black_epoch)return 1;
  flow.black_generation=s;flow.remaining=(u64)BSP_CLINT_HZ*300u/1000u;flow.last=now;
  flow_phase(FL_BLACK_HOLD);en_since=now;
 }
 if(flow.paused){if(en_stop_pending)en_stop_done();return 1;}
 if(flow.phase==FL_BLACK_HOLD){
  flow_hold(now);if(flow.remaining)return 1;
  flow_phase(FL_PRESSURE_PREP);en_since=now;return 1;
 }
 if(flow.phase==FL_PRESSURE_PREP){
  r=game_prepare_surface();if(!r)return 1;
  if(r<0 || flow_profile(1)!=GPU_OK){en_error=7;return 1;}
  if(enemy_scene_phase_begin()!=GPU_OK || player_scene_phase_begin()!=GPU_OK || player_scene_next(now)!=GPU_OK || bullet_scene_begin(now)!=GPU_OK || missile_scene_begin(now)!=GPU_OK || player_bullet_scene_begin(now)!=GPU_OK || effect_scene_phase_begin(now)!=GPU_OK){en_error=7;return 1;}
  decor_scene_begin(now);decor_scene_collect();
  game_reset_time(now);en_started=0;en_generation=gpu_get_display_generation();en_frame_motion=0;en_split_seen=0;
  flow_phase(FL_PRESSURE_FIRST);en_enter(EN_CLAIM);return 1;
 }
 return 1;
}
/* ---- CMD standalone application -------------------------------------------------------------------------------------
 * Power-up default APP ON: wait until IDs 1..33 are valid and the display is safely idle, then BEGIN the start menu (scene WAVE0
 * = multi mode without enemies, I4 background with the start-menu source, one static confirmed frame, no HUD). KEY3 (debounced
 * press after a release) -> EXIT (existing restore: black idle) -> FLOW LIGHT_PRESSURE MOVE (LIGHT = WAVE8; 5 displayed
 * player-bullet kills -> PRESSURE = WAVE40 target with PIPE ON; once per round). After a displayed game over KEY3 -> RESTART
 * (existing R2 path; FLOW returns to LIGHT, kills restart from 0). While APP is not OFF, host ENEMY configuration / session
 * commands, PLAYER CONNECT/INPUT and PLAYER SHIELD are refused (one control source); read-only queries, PLAYER JOY and
 * ENEMY APP stay available. ENEMY APP OFF exits any APP session and returns to the host-tool workflow; ENEMY APP ON re-arms. */
enum { APP_OFF,APP_WAIT,APP_MENU_BEGIN,APP_MENU,APP_MENU_EXIT,APP_GAME,APP_EXIT };
static const char *const app_names[]={"OFF","WAIT_ASSETS","MENU_BEGIN","MENU","MENU_EXIT","GAME","EXIT"};
static const char *app_name(void){return app_names[app_state];}
int enemy_test_app_in_menu(void){return app_state==APP_MENU && en_state==EN_SHOWN;} /* GHD: menu-only gestures */
int enemy_test_app_started(void){return app_starts!=0u;} /* GHD3S: the first game start closes the serial upload path */
/* GHD3S board metrics (HUD BUILD field): long intervals, longest interval; compose / critical prep maxima are cleared by the read. */
void enemy_test_frame_metrics(uint32_t v[4]){v[0]=en_gap25;v[1]=en_interval_max;v[2]=en_hud_comp_max;v[3]=en_hud_prep_max;en_hud_comp_max=en_hud_prep_max=0;}
uint32_t enemy_test_transition_us(void){return en_transition_us;} /* GHD4: last light->pressure transition (us) */
static int app_query(const char *s){
 static const char *const q[]={"STATUS","PERF","INTERVALS","EFFECTS","RICH","RICH POOL","DECOR","PIPE","BUILD","FLOW","MISSILES STATUS","FRAMES","BACKGROUND","KEY","STATS","STATS RESET"};
 for(unsigned i=0;i<sizeof(q)/sizeof(q[0]);i++)if(strcmp(s,q[i])==0)return 1;
 return 0;
}
static void app_enter(u32 s){app_state=s;en_text("ENEMY APP STATE=");en_text(app_names[s]);en_text("\r\n");}
static int app_start_permit(void){return (app_state==APP_MENU && en_state==EN_SHOWN) || (app_state==APP_GAME && enemy_test_active() && game_displayed_over());}
static int app_cmd(const char *s){int r;app_internal=1;r=en_dispatch_line(s,0,0);app_internal=0;return r;}
#ifndef BUILD_VARIANT_GAME /* GHD: UART-only */
static void app_status(void){
 en_text("ENEMY APP STATE=");en_text(app_name());en_text(" STARTS=");en_decimal(app_starts);en_text(" RESTARTS=");en_decimal(app_restarts);
 en_text(" ERRORS=");en_decimal(app_errors);en_text(" KEYS_HW=");en_decimal(game_keys_hw_present());en_text(" KEYS=");en_decimal(game_keys_stable());
 en_text(" FLOW=");en_text(flow_names[flow.phase]);en_text(" PB_KILLS=");en_decimal(combat_displayed_pb_kills());en_text(" THRESHOLD=");en_decimal(FLOW_PB_KILLS);
 en_text(" ASSETS=");uart_reply_writeHex(BSP_UART_TERMINAL,(int)asset_upload_valid_mask());en_text(",");uart_reply_writeHex(BSP_UART_TERMINAL,(int)asset_upload_valid_mask_hi());en_text("\r\n");
}
#ifndef BUILD_VARIANT_GAME
static int app_command(const char *s){
 if(strcmp(s,"APP")==0){app_status();return 1;}
 if(strcmp(s,"APP OFF")==0){
  if(app_state==APP_OFF){en_text("ENEMY APP OFF\r\n");return 1;}
  app_off_request=1; /* the EXIT runs in enemy_test_app_service(), never nested in this line's dispatch */
  if(app_state==APP_WAIT){app_off_request=0;app_enter(APP_OFF);return 1;}
  en_text("ENEMY APP OFF ACCEPTED\r\n");return 1;
 }
 if(strcmp(s,"APP ON")==0){
  if(app_state!=APP_OFF){en_text("ENEMY APP ON\r\n");return 1;}
  if(!game_keys_hw_present()){en_text("ENEMY APP UNSUPPORTED_HW\r\n");return 1;}
  if(enemy_test_active()){en_text("ENEMY APP BUSY SESSION_ACTIVE\r\n");return 1;}
  app_off_request=0;app_due=0;app_enter(APP_WAIT);return 1;
 }
 en_text("ENEMY APP ERROR PARAM\r\n");return 1;
}
#endif
#endif /* !BUILD_VARIANT_GAME */
static void app_fail(u64 now){app_errors++;app_due=now+(u64)BSP_CLINT_HZ;}
static gpu_result_t app_menu_profile(void){
 flow.enabled=0;flow.phase=FL_OFF;en_pipe=0;
 if(enemy_scene_select(ENEMY_SCENE_WAVE0)!=GPU_OK || missile_scene_select(0)!=GPU_OK || bullet_scene_select(0)!=GPU_OK ||
    player_bullet_scene_select(0)!=GPU_OK || effect_shield_select(SHIELD_OFF)!=GPU_OK || effect_scene_select(0)!=GPU_OK ||
    player_scene_select(0)!=GPU_OK || decor_scene_select(0)!=GPU_OK)return GPU_ERR_PARAM;
 game_select(0);en_background=GPU_BACKGROUND_I4;return gpu_enemy_key_configure(0);
}
void enemy_test_app_service(int peer_busy){
 u64 now=clint_getTime(BSP_CLINT);int r;
 if(renderer_cpu_sync_waiting())return;
 if(app_off_request && app_state!=APP_OFF && app_state!=APP_EXIT){
  if(enemy_test_active()){r=app_cmd("ENEMY EXIT");if(r<0)return;}
  app_enter(APP_EXIT);return;
 }
 if(now<app_due)return;
 switch(app_state){
 case APP_WAIT:
  if(!game_keys_hw_present()){app_enter(APP_OFF);return;} /* not the CMD bit: host-tool workflow only */
  if(peer_busy || enemy_test_active() || !game_lifecycle_safe() || asset_upload_active())return;
  if((asset_upload_valid_mask()&0xfffffffeu)!=0xfffffffeu || (asset_upload_valid_mask_hi()&MENU_REQUIRED_ASSETS_HI)!=MENU_REQUIRED_ASSETS_HI)return;
  if(app_menu_profile()!=GPU_OK){app_fail(now);return;}
  r=app_cmd("ENEMY BEGIN");if(r<0)return;
  if(enemy_test_active())app_enter(APP_MENU_BEGIN);else app_fail(now);
  return;
 case APP_MENU_BEGIN:
  if(!enemy_test_active()){app_fail(now);app_enter(APP_WAIT);return;}
  if(en_error || en_state==EN_FAILED){(void)app_cmd("ENEMY EXIT");app_fail(now);app_enter(APP_EXIT);return;}
  if(en_state==EN_SHOWN && en_frames)app_enter(APP_MENU);
  return;
 case APP_MENU:
  if(!enemy_test_active()){app_enter(APP_WAIT);return;}
  if(game_keys_take_start()){joystick_restart_recover(now);r=app_cmd("ENEMY EXIT");if(r<0)return;app_enter(APP_MENU_EXIT);} /* CMDJ */
  return;
 case APP_MENU_EXIT:
  if(peer_busy || enemy_test_active() || !game_lifecycle_safe())return;
  flow.enabled=1;flow.phase=FL_OFF;en_pipe=1;
  r=app_cmd("ENEMY MOVE");if(r<0)return;
  if(enemy_test_active()){app_starts++;app_enter(APP_GAME);}else{app_fail(now);app_enter(APP_WAIT);}
  return;
 case APP_GAME:
  if(!enemy_test_active()){app_enter(APP_WAIT);return;}
  if(en_state==EN_FAILED || en_state==EN_RESTORE_FAILED){(void)app_cmd("ENEMY EXIT");app_fail(now);app_enter(APP_EXIT);return;}
  if(game_keys_take_start()){joystick_restart_recover(now);r=app_cmd("ENEMY RESTART");if(r>=0)app_restarts++;} /* CMDJ */
  return;
 case APP_EXIT:
  if(enemy_test_active() || !game_lifecycle_safe())return;
  if(app_off_request){app_off_request=0;app_enter(APP_OFF);}else app_enter(APP_WAIT);
  return;
 default:return;
 }
}
void enemy_test_service(int guard){
 uint32_t s;int r;
 if(guard || !enemy_test_active())return;
 if(en_exit && en_state!=EN_DRAINING && en_state!=EN_RESTORING && en_state!=EN_RESTORE_FAILED){
  if(en_started)en_control(2);
  en_enter(EN_DRAINING);
 }
 if(en_error && flow.enabled && flow.phase>=FL_DRAIN && flow.phase<FL_PRESSURE && !en_exit){en_moving=0;flow_phase(FL_FAILED);en_enter(EN_FAILED);}
 if(flow_service())return;
 switch(en_state){
 case EN_PREPARING:
  r=en_hooks.prepare();
  if(r<0 || en_timeout()){en_error=1;en_moving=0;en_enter(EN_FAILED);}
  else if(r>0){
   combat_init();en_pipe_discard();en_pipe_preps=en_pipe_frames=en_pipe_discards=en_pipe_prep_max_us=0;en_pipe_prep_sum_us=0;
   /* begin selects multi mode and collects CPU descriptors only; the actual
    * descriptor ports are opened later in EN_CLAIM, after PL is configured. */
   game_begin(clint_getTime(BSP_CLINT));en_stats_session(clint_getTime(BSP_CLINT));bullet_scene_set_fire(1);missile_scene_set_fire(1);player_bullet_scene_set_fire(1);en_restart_pending=0;
   if(enemy_scene_begin()!=GPU_OK || player_scene_begin()!=GPU_OK || bullet_scene_begin(clint_getTime(BSP_CLINT))!=GPU_OK || missile_scene_begin(clint_getTime(BSP_CLINT))!=GPU_OK || player_bullet_scene_begin(clint_getTime(BSP_CLINT))!=GPU_OK || effect_scene_begin(clint_getTime(BSP_CLINT))!=GPU_OK){en_error=7;en_moving=0;en_enter(EN_FAILED);break;}
   decor_scene_begin(clint_getTime(BSP_CLINT));decor_scene_collect(); /* first (static) frame: enable + empty slots */
   /* The first motion frame is still static. Arm all emitters from this
    * common CLINT sample, after prepare has finished. */
   {u64 now=clint_getTime(BSP_CLINT);enemy_scene_reset_time(now);bullet_scene_reset_time(now);missile_scene_reset_time(now);decor_scene_reset_time(now);player_bullet_scene_reset_time(now);effect_scene_reset_time(now);game_reset_time(now);}
   en_generation=gpu_get_display_generation();
   if(en_read(ENEMY_REG_ABI)==ENEMY_ABI)write_u32(0,IO_APB_SLAVE_0_INPUT+ENEMY_REG_Y);
   en_enter(EN_CLAIM);
  }
  break;
 case EN_CLAIM:
  /* A service invocation performs at most sixteen table data writes.
   * Seal enemies first; start bullet service on a subsequent invocation. */
  r=enemy_scene_stage_service();if(r==1)r=bullet_scene_stage_service();if(r==1)r=player_bullet_scene_stage_service();if(r==1)r=effect_scene_stage_service();if(r==1)r=missile_scene_stage_service();if(r<0){en_error=7;en_moving=0;en_enter(EN_FAILED);break;}if(!r)break;
  s=en_read(ENEMY_REG_STATUS);
  if(s&1u){en_started=1;if(en_pipe_on())en_pipe_claimed();en_enter(EN_DRAWING);}
  else if(en_timeout()){en_error=2;en_moving=0;en_enter(EN_FAILED);}
  else {en_submit_time=clint_getTime(BSP_CLINT);
   if(en_prep_open){u64 us=en_us(en_submit_time-en_prep_start);en_prep_us=us>0xffffffffu?0xffffffffu:(uint32_t)us;
    if(en_prep_us>en_prep_max_us){en_prep_max_us=en_prep_us;}en_prep_open=0;}
   en_control(1);}
  break;
 case EN_DRAWING:
  s=en_read(ENEMY_REG_STATUS);
  if((s&8u) && !en_split_seen){en_present_time=clint_getTime(BSP_CLINT);en_split_seen=1;}
  if((s&32u) || en_timeout()){
   en_error=(s&32u)?en_read(ENEMY_REG_ERROR):3u;en_moving=0;
   en_control(2);en_enter(EN_DRAINING);
  }else if((s&16u) && gpu_get_display_generation()!=en_generation &&
           (gpu_get_display_status()&15u)==8u){
   u64 now=clint_getTime(BSP_CLINT);en_frames++;
   en_display_generation=gpu_get_display_generation();enemy_scene_confirmed();bullet_scene_confirmed();missile_scene_confirmed();player_scene_confirmed();player_bullet_scene_confirmed();effect_scene_confirmed();decor_scene_confirmed();combat_confirmed();game_confirmed();
   bullet_stats_confirmed(bullet_scene_displayed_visible(),missile_scene_displayed_visible(),decor_scene_displayed_visible(),player_bullet_scene_displayed_visible(),bullet_scene_displayed_groups());
   game_hud_confirmed(en_frames,enemy_scene_displayed_objects()+bullet_scene_displayed_visible()+missile_scene_displayed_visible()+player_scene_displayed_visible()+player_bullet_scene_displayed_visible());
   game_hud_set_game(game_selected(),game_displayed_lives(),game_displayed_score(),game_displayed_over());
   en_hud2_fields();
   uint32_t en_tel_iv=0; /* GHDA: interval of this shown frame (0 = first of a run) */
   if(en_frame_motion){
    if(en_run_armed){
     u64 us=en_us(now-en_run_end);uint32_t interval=us>0xffffffffu?0xffffffffu:(uint32_t)us;
#ifndef BUILD_VARIANT_GAME
     en_interval_us[en_intervals&63u]=interval;
#endif
     if(!en_intervals || interval<en_interval_min)en_interval_min=interval;
     if(interval>en_interval_max)en_interval_max=interval;
     if(interval>25000u)en_gap25++;
     en_intervals++;en_tel_iv=interval;
    }
    if(!en_run_armed){en_run_start=en_submit_time;en_run_armed=1;}
    en_run_frames++;en_run_end=now;
    {u64 floor=en_prep_start+EN_START_MIN_TICKS;en_due=now;
     if(now<floor){en_due=floor;enemy_test_diag_event(EN_DIAG_FLOOR,0);}}
   }
   en_frame_ticks=now-en_submit_time;en_last_split=en_split_seen;
   en_comp_ticks=en_split_seen?en_present_time-en_submit_time:0;
   en_present_ticks=en_split_seen?now-en_present_time:0;
   en_diag_confirm(now);
   {u64 cu=en_us(en_comp_ticks);if(cu>en_hud_comp_max)en_hud_comp_max=cu>0xffffffffu?0xffffffffu:(uint32_t)cu;if(en_prep_us>en_hud_prep_max)en_hud_prep_max=en_prep_us;
    en_tel_frame(en_tel_iv,cu>0xffffffffu?0xffffffffu:(uint32_t)cu,en_prep_us);}
   en_enter(EN_SHOWN);
   if(flow.enabled && flow.phase==FL_PRESSURE_FIRST){
    flow.pressure_generation=en_display_generation;flow_phase(FL_PRESSURE);
    enemy_scene_reset_time(now);bullet_scene_reset_time(now);missile_scene_reset_time(now);decor_scene_reset_time(now);player_bullet_scene_reset_time(now);effect_scene_reset_time(now);game_reset_time(now);
    en_motion_time=now;en_due=now+EN_FRAME_TICKS;game_hud_begin();
    en_gap25=en_interval_max=en_hud_comp_max=en_hud_prep_max=0; /* GHD3S: board metrics count the pressure phase */
    /* GHD4 (S14): steady state starts at the first pressure motion frame: the interval timer is disarmed here, so the transition
     * (last light frame -> this first pressure frame) is never counted as a steady-state long frame; it is kept separately. */
    en_run_armed=0;{u64 t=en_transition_start&&now>=en_transition_start?en_us(now-en_transition_start):0;en_transition_us=t>0xffffffffu?0xffffffffu:(uint32_t)t;}
    game_hud_confirmed(en_frames,enemy_scene_displayed_objects()+player_scene_displayed_visible());
     game_hud_set_game(game_selected(),game_displayed_lives(),game_displayed_score(),game_displayed_over());en_hud2_fields();
   }
   if(flow.restarting){flow.restarting=0;en_text("ENEMY RESTARTED\r\n");}
   if(flow.enabled && flow.phase==FL_LIGHT && !flow.triggered && !game_displayed_over() && combat_displayed_pb_kills()>=FLOW_PB_KILLS){
    flow.triggered=1;flow.trigger_generation=en_display_generation;player_input_revoke(now);flow_phase(FL_DRAIN);en_transition_start=now;
   }
   if(en_frames==1)en_text("ENEMY SHOWN\r\n");
   if(!en_moving && !en_pipe_ready && (!flow.enabled || flow.phase==FL_LIGHT || flow.phase==FL_PRESSURE)){if(flow.enabled && (en_read(ENEMY_REG_STATUS)&1u)){en_control(2);en_enter(EN_RECYCLE);}else en_stop_done();}
  }
  /* S: compute N+1 once per claim, while PL composes N */
  if(en_state==EN_DRAWING && en_pipe_on() && !en_pipe_ready && en_moving && !en_stop_pending && !en_restart_pending && !en_exit && !en_error)en_pipe_prep();
  /* GHD3S: stage + seal N+1 into the stage banks while N is composed (one bounded round per pass; see scripts/_early_stage.py) */
  else if(en_state==EN_DRAWING && en_pipe_ready && !en_pipe_staged && !en_restart_pending && !en_exit && !en_error){
   if(en_pipe_stage_round()<0){en_error=7;en_moving=0;en_control(2);en_enter(EN_DRAINING);}
  }
  break;
 case EN_SHOWN:
  if(en_restart_pending){
   /* Reseed one round in place: pools, score, lives and timers. Display ownership is unchanged. */
   u64 now=clint_getTime(BSP_CLINT);s=en_read(ENEMY_REG_STATUS);
   if(s&1u){en_control(2);break;}
   if(!(s&128u))break;
   /* Multi-mode writes interlock with every segment mode, even when writing
    * the current value. Retire staged tables and disable segments only after
    * the old owner and writer have drained, before begin selects multi mode. */
   en_pipe_discard();
   enemy_scene_cancel();bullet_scene_cancel();missile_scene_cancel();player_bullet_scene_cancel();effect_scene_cancel();
   if(missile_scene_disable()!=GPU_OK || effect_scene_disable()!=GPU_OK || player_bullet_scene_disable()!=GPU_OK || bullet_scene_disable()!=GPU_OK || player_scene_disable()!=GPU_OK){en_error=7;en_moving=0;en_enter(EN_FAILED);break;}
   if(flow.enabled && flow_profile(0)!=GPU_OK){en_error=7;en_moving=0;en_enter(EN_FAILED);break;}
   if(flow.enabled){flow.round++;flow.triggered=0;flow.black_epoch=flow.black_generation=flow.pressure_generation=0;flow.restarting=1;flow_phase(FL_LIGHT);}
   en_restart_pending=0;combat_init();game_begin(now);en_stats_session(now);bullet_scene_set_fire(1);missile_scene_set_fire(1);player_bullet_scene_set_fire(1);
   player_input_revoke(now);
   if(enemy_scene_begin()!=GPU_OK || player_scene_begin()!=GPU_OK || bullet_scene_begin(now)!=GPU_OK || missile_scene_begin(now)!=GPU_OK || player_bullet_scene_begin(now)!=GPU_OK || effect_scene_begin(now)!=GPU_OK){en_error=7;en_moving=0;en_enter(EN_FAILED);break;}
   decor_scene_begin(now);decor_scene_collect();
   enemy_scene_reset_time(now);bullet_scene_reset_time(now);missile_scene_reset_time(now);decor_scene_reset_time(now);player_bullet_scene_reset_time(now);effect_scene_reset_time(now);game_reset_time(now);
   /* A new round starts a new RUN/INTERVALS window (the reseeded first frame is static). */
   en_run_frames=0;en_run_armed=0;en_run_start=en_run_end=now;
   en_intervals=en_gap25=en_interval_min=en_interval_max=0;en_prep_max_us=0;en_diag_reset();
   en_started=0;en_generation=gpu_get_display_generation();en_frame_motion=0;en_split_seen=0;en_enter(EN_CLAIM);if(!flow.enabled)en_text("ENEMY RESTARTED\r\n");
   break;
  }
  /* GHD3S: N was confirmed before the early staging of N+1 finished: finish it first (never publish over a table mid-staging) */
  if(en_pipe_ready && !en_pipe_staged){r=en_pipe_stage_round();if(r<0){en_error=7;en_moving=0;en_enter(EN_FAILED);break;}if(!r)break;}
  if((en_moving || en_pipe_ready) && clint_getTime(BSP_CLINT)>=en_due){
   s=en_read(ENEMY_REG_STATUS);
   if(s&1u){en_control(2);en_enter(EN_RECYCLE);}
   else if(s&128u){en_started=0;if(en_pipe_ready)en_pipe_publish();else en_next_frame();}
  }
  break;
 case EN_RECYCLE:
  if(en_pipe_ready && !en_pipe_staged){r=en_pipe_stage_round();if(r<0){en_error=7;en_moving=0;en_enter(EN_FAILED);break;}if(!r)break;}
  s=en_read(ENEMY_REG_STATUS);
  if(!(s&1u) && (s&128u)){
   en_started=0;
   if(en_pipe_ready)en_pipe_publish();else if(en_moving)en_next_frame();else{en_enter(EN_SHOWN);en_stop_done();}
  }else if(en_timeout()){
   en_error=4;en_moving=0;
   en_enter(EN_DRAINING);en_text("ENEMY ERROR OWNER_HELD\r\n");
  }
  break;
 case EN_DRAINING:
  s=en_read(ENEMY_REG_STATUS);
  // Claim can race EXIT after a control write. Always re-read real ownership.
  if(s&1u){en_started=1;en_control(2);if(en_timeout() && en_error!=4u){en_error=4;en_text("ENEMY ERROR OWNER_HELD\r\n");}}
  else if((s&128u) || !en_started){
   if(!en_exit){en_enter(EN_FAILED);en_text("ENEMY FAILED\r\n");break;}
   en_pipe_discard();
   enemy_scene_cancel();bullet_scene_cancel();missile_scene_cancel();player_bullet_scene_cancel();effect_scene_cancel();
   if(missile_scene_disable()!=GPU_OK || effect_scene_disable()!=GPU_OK || player_bullet_scene_disable()!=GPU_OK || bullet_scene_disable()!=GPU_OK || player_scene_disable()!=GPU_OK){en_error=7;en_enter(EN_RESTORE_FAILED);en_text("ENEMY RESTORE_FAILED\r\n");break;}
   decor_scene_disable();
   if(!en_restore_registered){
    if(flow.enabled && flow.black_epoch && game_idle_epoch()==flow.black_epoch && game_lifecycle_busy() && game_idle_confirmed()==0)break;
    game_hud_end();
    r=en_hooks.restore(en_mode,en_source);
    if(r<0){en_error=5;en_enter(EN_RESTORE_FAILED);en_text("ENEMY RESTORE_FAILED\r\n");}
    else{en_restore_registered=1;en_enter(EN_RESTORING);en_text("ENEMY RESTORING\r\n");}
   }
  }
  break;
 case EN_RESTORING:
  r=en_hooks.restored();
  if(r>0){enemy_scene_restored();bullet_scene_restored();missile_scene_restored();player_scene_restored();player_bullet_scene_restored();effect_scene_restored();combat_restored();game_restored();en_restart_pending=0;en_error=0;if(flow.enabled)flow_phase(FL_OFF);en_enter(EN_EXITED);en_text("ENEMY EXITED\r\n");}
  else if(r<0 || en_timeout()){en_error=6;en_enter(EN_RESTORE_FAILED);en_text("ENEMY RESTORE_FAILED\r\n");}
  break;
 default:break;
 }
}
