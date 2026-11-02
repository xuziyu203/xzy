/* GHD3S single ELF: asset upload over the UART + the complete game, no other serial use.
 * Flow: load ELF -> confirmed black idle frame -> upload IDs 1..33 (tools/upload_single.py; each asset is read back from DDR with the
 * hardware CRC reader before it is registered) -> all valid: the APP state machine shows the start menu -> KEY3 starts the game.
 * The UART receive side is serviced only until the first game start: from then on no byte is read or parsed (re-upload = reload).
 * GHDA: while a game runs, one telemetry line per second is written (TX only, never waits; see tel_service).
 * In the menu ASSET BEGIN is refused (assets cannot change under a running session); ASSET STATUS / ASSET BUILD still answer.
 * HUD2 BUILD field: "UP nn" (registered assets) while uploading, "UPERR" after a failed upload, then the density tier tag.
 * Menu keys: KEY2 press = next density tier (L -> M -> H; also on the GAME OVER screen, where KEY2 has no shield function),
 * KEY1 held 2 s with the stick released = joystick centre calibration
 * ("CAL" / "CALNG"); KEY3 = start. In game: KEY1 fire, KEY2 shield on/off, KEY3 restart after game over. */
#if defined(BUILD_VARIANT_GAME) && defined(ASSET_UPLOAD_SERIAL)
#include "gpu.h"
#include "renderer.h"
#include "asset_upload.h"
#include "enemy_test.h"
#include "game_hud.h"
#include "game_lifecycle.h"
#include "game_keys.h"
#include "joystick_ads1115.h"
#include "bullet_scene.h"
#include "player_scene.h"
#include "decor_scene.h"
#include "combat.h"
#include "effect_scene.h"
#include "joystick_ads1115.h"
#include "game.h"
#include "bullet_stats.h"
#include "io.h"
#include "soc.h"
#include "build_id.h"
#include "bsp.h"
#include <stdint.h>
#include <string.h>
#define GAME_JOY_CENTER_X 12910u /* GHD4: measured at rest on this board 2026-10-31 (load_20261031/attempt6/joy_check.json: 12910/12977) */
#define GAME_JOY_CENTER_Y 12977u
#define GAME_JOY_INVERT 0u
#define LINE_MAX_LEN 96u
static char line[LINE_MAX_LEN];
static unsigned line_len, line_overflow, serial_closed, panel_on, metric_index, metric_shown;
static u64 metric_due;
static uint32_t shown_count = 0xffffffffu, shown_error;
static u64 cal_since, tag_until;
/* GHD4 tier tags: 'M-' + density number: M-100, M-106, M-110 (all tiers keep the M
 * style); the '-' keeps them distinct from the missile metric 'Mnnnn' of the BUILD-field cycle. The number is the measured average
 * on-screen density relative to the original M (config density_tiers.tag_pct). */
static char tier_tag[BULLET_TIER_COUNT][6];
static void tier_tags_init(void){static const uint16_t tag[3]=BULLET_DENSITY_TAG_PCT;unsigned i;
 for(i=0;i<3u;i++){unsigned c=tag[i];tier_tag[i][0]='M';tier_tag[i][1]='-';tier_tag[i][2]=(char)('0'+(c/100u)%10u);tier_tag[i][3]=(char)('0'+(c/10u)%10u);tier_tag[i][4]=(char)('0'+c%10u);tier_tag[i][5]=0;}
#if PROBE_LADDER_ENABLED
 memcpy(tier_tag[BULLET_TIER_LADDER],LADDER_TIER_TAG,sizeof(LADDER_TIER_TAG)); /* GHDA probe ladder tier */
#endif
}
/* GHDA: tag of the selected tier; the probe ladder shows its current level tag while it runs */
static const char *tier_tag_now(void){u32 t=bullet_scene_density_tier();
#if PROBE_LADDER_ENABLED
 if(t==BULLET_TIER_LADDER)return bullet_scene_ladder_tag();
#endif
 return tier_tag[t<BULLET_TIER_COUNT?t:BULLET_DENSITY_DEFAULT];}
static uint32_t metric_x, metric_c, metric_r;
static unsigned valid_count(void){
 uint32_t v=asset_upload_valid_mask()&0xfffffffeu,h=asset_upload_valid_mask_hi()&3u;unsigned n=0;
 while(v){n+=v&1u;v>>=1;} while(h){n+=h&1u;h>>=1;} return n;
}
static void serial_line(const char *s){
 while(*s==' ' || *s=='\t')s++;
 if(strncmp(s,"ASSET BEGIN",11)==0 && (enemy_test_active() || game_lifecycle_busy())){uart_writeStr(BSP_UART_TERMINAL,"ASSET ERROR BUSY\r\n");return;}
 if(asset_upload_handle_line(s))return;
 uart_writeStr(BSP_UART_TERMINAL,"ERR UPLOAD_ONLY\r\n");
}
/* The only UART reader. Same routing rule as the former demo_uart_poll for the asset protocol: binary packet bytes go to the
 * asset state machine, text lines to the ASSET command parser; never drained inside a synchronous writer callback. */
static void serial_poll(int guard){
 unsigned budget=64u;
 asset_upload_set_resource_guard(guard);
 asset_upload_service();
 if(guard){asset_upload_set_resource_guard(0);return;}
 while(budget--){
  uint8_t c;
  if(!uart_readOccupancy(BSP_UART_TERMINAL))break;
  c=(uint8_t)uart_read(BSP_UART_TERMINAL);
  if(asset_upload_route_byte(c)){(void)asset_upload_consume_byte(c);continue;}
  if(c=='\r' || c=='\n'){
   if(line_len || line_overflow){line[line_len]=0;if(line_overflow)uart_writeStr(BSP_UART_TERMINAL,"ERR LINE_TOO_LONG\r\n");else serial_line(line);}
   line_len=line_overflow=0;
  }else if(!line_overflow){if(line_len<LINE_MAX_LEN-1u)line[line_len++]=(char)c;else line_overflow=1;}
 }
 asset_upload_set_resource_guard(0);
}
/* letter + 4 digits, saturated */
static void metric_count(char *s,char letter,uint32_t v){unsigned k;if(v>9999u)v=9999u;s[0]=letter;for(k=4u;k>0u;k--){s[k]=(char)('0'+v%10u);v/=10u;}s[5]=0;}
/* letter + nn.n milliseconds from microseconds, saturated */
static void metric_ms(char *s,char letter,uint32_t us){uint32_t t=(us+50u)/100u;if(t>999u)t=999u;s[0]=letter;s[1]=(char)('0'+t/100u);s[2]=(char)('0'+(t/10u)%10u);s[3]='.';s[4]=(char)('0'+t%10u);s[5]=0;}
/* compositor cycle buckets of the last frame (enemy_compositor.v PERF_DETAIL=1, core_clk 100 MHz): AR, R, FIFO, AW, W and B waits */
static uint32_t ddr_wait_us(void){
 static const uint8_t b[10]={1,2,5,6,7,10,11,12,13,15};uint32_t c=0;unsigned k;
 for(k=0;k<10u;k++)c+=read_u32(IO_APB_SLAVE_0_INPUT+0x190u+4u*b[k]);
 return c/100u;
}
static void metric_next(void){
 const bullet_stats_t *st=bullet_stats_displayed();uint32_t m[4];char s[6];unsigned i=metric_index;metric_index=(metric_index+1u)%12u;
 switch(i){
 case 0:game_hud_set_tag(tier_tag_now());metric_shown=0u;return;
 case 1:metric_count(s,'A',st->air);break;
 case 2:metric_count(s,'M',st->missile);break;
 case 3:metric_count(s,'T',st->turret);break;
 case 4:metric_count(s,'P',st->player);break;
 case 5:enemy_test_frame_metrics(m);metric_count(s,'L',m[0]);metric_x=m[1];metric_c=m[2];metric_r=m[3];break;
 case 6:metric_count(s,'X',(metric_x+500u)/1000u);break;
 case 7:metric_ms(s,'C',metric_c);break;
 case 8:metric_ms(s,'R',metric_r);break;
 case 9:if(read_u32(IO_APB_SLAVE_0_INPUT+0x184u)==0x45500103u)metric_ms(s,'W',ddr_wait_us());else{s[0]='W';s[1]=s[2]=s[3]=s[4]='-';s[5]=0;}break;
 case 10:metric_count(s,'N',st->groups);break; /* GHD6: active bullet groups of the shown frame (850-group tier) */
 default:metric_count(s,'G',(enemy_test_transition_us()+500u)/1000u);break; /* GHD4: light->pressure transition, ms */
 }
 game_hud_set_tag(s);metric_shown=1u;
}
static void status_service(u64 now){
 if(game_lifecycle_safe() && !enemy_test_active() && !game_hud_owns_panel())game_hud_status_panel();
 panel_on=game_hud_is_hud2();
 if(!panel_on)return;
 if(!enemy_test_active() && !serial_closed){
  unsigned n=valid_count();uint32_t e=asset_upload_error();
  if(n!=shown_count || e!=shown_error){
   char s[6]={'U','P',' ',(char)('0'+(n/10u)%10u),(char)('0'+n%10u),0};
   game_hud_set_tag(e && !asset_upload_active()?"UPERR":s);shown_count=n;shown_error=e;
  }
  return;
 }
 if(tag_until){if(now>=tag_until){tag_until=0;game_hud_set_tag(tier_tag_now());}return;}
 if(shown_count!=0xfffffffeu){shown_count=0xfffffffeu;game_hud_set_tag(tier_tag_now());}
 if(enemy_test_input_moving_public()){if(now>=metric_due){metric_due=now+(u64)BSP_CLINT_HZ*3u/2u;metric_next();}}
 else if(metric_index || metric_shown){metric_index=0;metric_shown=0;metric_due=0;game_hud_set_tag(tier_tag_now());}
}
static void menu_service(u64 now){
 const joystick_status_t *js=joystick_status();
 int over=enemy_test_active() && game_displayed_over() && !enemy_test_input_moving_public();
 if(!enemy_test_app_in_menu() && !over){cal_since=0;(void)game_keys_take_menu_key2();return;}
 if(game_keys_take_menu_key2()){
  bullet_scene_density_select((bullet_scene_density_tier()+1u)%BULLET_TIER_COUNT);
  game_hud_set_tag(tier_tag_now());tag_until=0;
 }
 if(!enemy_test_app_in_menu()){cal_since=0;return;}
 /* KEY1 (stable) held 2 s with a fresh raw pair near the accepted centre = new centre; a pushed stick is rejected. */
 if((game_keys_stable()&6u)==2u && js->valid){
  if(!cal_since)cal_since=now;
  else if(now-cal_since>=(u64)BSP_CLINT_HZ*2u){
   const u32 c[3]={(u32)js->raw[0],(u32)js->raw[1],GAME_JOY_INVERT};
   int near=js->raw[0]>(int)GAME_JOY_CENTER_X-3000 && js->raw[0]<(int)GAME_JOY_CENTER_X+3000 && js->raw[1]>(int)GAME_JOY_CENTER_Y-3000 && js->raw[1]<(int)GAME_JOY_CENTER_Y+3000;
   game_hud_set_tag(near && joystick_center(now,c)?"CAL":"CALNG");tag_until=now+(u64)BSP_CLINT_HZ*2u;cal_since=0;
  }
 }else cal_since=0;
}
/* GHDA telemetry (TX only): while a game runs, one line per second
 *   TEL <s> <tier/level tag> <L|S> fr=<shown frames> lf=<intervals > 25 ms> im=<max interval us> c=<max compose us> r=<max prep us>
 *   b=<real bullets avg/min/max> g=<groups avg/max> gl=<glow avg x.y/max> hit=<player hits> abs=<shield absorbs> sc=<score> hp=<HP>
 *   jf=<joystick faults in the second>[:<reason>x<n>,...] jl=<latched> ja=<armed> jv=<valid> jg=<ms since the last fresh pair>
 *   jp=<pairs in the second> jr=<recovery attempts>/<ok>/<fail>/<last attempt ms> jb=<bus recover commands>
 *   js=<joystick service calls> jx=<max joystick service gap us> jn=<pair attempts started> rp=<pipelined prep max us> [dr=<dropped lines>]
 * "TEL BEGIN <BUILD> tier=<tag>" at the first game frame, "TEL END" when the game stops. Bytes are written only while the UART TX
 * FIFO has room (never waits) and never inside the writer-wait callback; a line that does not fit the ring is dropped whole (dr=). */
#define TEL_RING 2048u
static char tel_ring[TEL_RING];
static unsigned tel_head,tel_count,tel_on;
static u64 tel_next;static uint32_t tel_sec,tel_drop;
static uint32_t tel_jf,tel_jc[JOY_DIAG_CLASS_COUNT],tel_jp,tel_ja,tel_jo,tel_jx,tel_jb,tel_js,tel_jn; /* GHDA: previous joystick counters */
static void tel_joy_mark(void){const joystick_diag_t *d=joystick_diag();const joystick_recovery_t *r=joystick_recovery();unsigned i;
 tel_jf=joystick_status()->faults;tel_jp=joystick_status()->pairs;for(i=0;i<JOY_DIAG_CLASS_COUNT;i++)tel_jc[i]=d->count[i];tel_ja=r->attempts;tel_jo=r->ok;tel_jx=r->fail;tel_jb=r->bus_recovers;tel_js=r->calls;tel_jn=r->pair_starts;}
static unsigned tel_u(char *p,uint32_t v){char b[10];unsigned n=0,k=0;do{b[n++]=(char)('0'+v%10u);v/=10u;}while(v);while(n)p[k++]=b[--n];return k;}
static unsigned tel_i(char *p,int32_t v){if(v<0){*p='-';return 1u+tel_u(p+1,(uint32_t)(-v));}return tel_u(p,(uint32_t)v);}
static unsigned tel_s(char *p,const char *s){unsigned k=0;while(s[k]){p[k]=s[k];k++;}return k;}
static void tel_commit(const char *b,unsigned n){unsigned i;if(TEL_RING-tel_count<n){tel_drop++;return;}for(i=0;i<n;i++)tel_ring[(tel_head+tel_count+i)%TEL_RING]=b[i];tel_count+=n;}
static void tel_pump(void){unsigned budget=32u;
 while(budget && tel_count && uart_writeAvailability(BSP_UART_TERMINAL)){uart_write(BSP_UART_TERMINAL,tel_ring[tel_head]);tel_head=(tel_head+1u)%TEL_RING;tel_count--;budget--;}}
static void tel_service(u64 now){
 /* Room for every existing TEL counter at its full uint32 width (including
  * four fault reasons). JOY reuses this buffer after the TEL is queued. */
 char b[768];unsigned n=0;enemy_test_telem_t t;
 if(!enemy_test_app_started() || !enemy_test_active()){if(tel_on){tel_on=0;n=tel_s(b,"TEL END\r\n");tel_commit(b,n);}return;}
 if(!tel_on){tel_on=1;tel_sec=0;tel_drop=0;tel_next=now+BSP_CLINT_HZ;enemy_test_telem_take(&t);
  tel_joy_mark();(void)joystick_take_gap_window();n=tel_s(b,"TEL BEGIN ");n+=tel_s(b+n,BUILD_ID);n+=tel_s(b+n," tier=");n+=tel_s(b+n,tier_tag_now());n+=tel_s(b+n,"\r\n");tel_commit(b,n);return;}
 if(now<tel_next)return;
 tel_next+=BSP_CLINT_HZ;if(tel_next<=now)tel_next=now+BSP_CLINT_HZ;tel_sec++;
 enemy_test_telem_take(&t);
 n=tel_s(b,"TEL ");n+=tel_u(b+n,tel_sec);b[n++]=' ';n+=tel_s(b+n,tier_tag_now());b[n++]=' ';b[n++]=t.stress?'S':'L';
 n+=tel_s(b+n," fr=");n+=tel_u(b+n,t.frames);n+=tel_s(b+n," lf=");n+=tel_u(b+n,t.long_frames);n+=tel_s(b+n," im=");n+=tel_u(b+n,t.imax_us);
 n+=tel_s(b+n," c=");n+=tel_u(b+n,t.cmax_us);n+=tel_s(b+n," r=");n+=tel_u(b+n,t.rmax_us);
 n+=tel_s(b+n," b=");n+=tel_u(b+n,t.frames?t.bul_sum/t.frames:0u);b[n++]='/';n+=tel_u(b+n,t.bul_min);b[n++]='/';n+=tel_u(b+n,t.bul_max);
 n+=tel_s(b+n," g=");n+=tel_u(b+n,t.frames?t.grp_sum/t.frames:0u);b[n++]='/';n+=tel_u(b+n,t.grp_max);
 {uint32_t g10=t.frames?(t.glow_sum*10u+t.frames/2u)/t.frames:0u;n+=tel_s(b+n," gl=");n+=tel_u(b+n,g10/10u);b[n++]='.';n+=tel_u(b+n,g10%10u);b[n++]='/';n+=tel_u(b+n,t.glow_max);}
 n+=tel_s(b+n," hit=");n+=tel_u(b+n,combat_displayed_player_hits());n+=tel_s(b+n," abs=");n+=tel_u(b+n,effect_shield_absorbed());
 n+=tel_s(b+n," sc=");n+=tel_u(b+n,game_displayed_score());n+=tel_s(b+n," hp=");n+=tel_u(b+n,game_displayed_lives());
 {const joystick_status_t *s=joystick_status();const joystick_diag_t *d=joystick_diag();const joystick_recovery_t *r=joystick_recovery();unsigned i,k=0;
  n+=tel_s(b+n," jf=");n+=tel_u(b+n,s->faults-tel_jf);
  for(i=0;i<JOY_DIAG_CLASS_COUNT && k<4u;i++)if(d->count[i]!=tel_jc[i]){b[n++]=k?',':':';n+=tel_u(b+n,i);b[n++]='x';n+=tel_u(b+n,d->count[i]-tel_jc[i]);k++;}
  n+=tel_s(b+n," jl=");b[n++]=s->latched?'1':'0';n+=tel_s(b+n," ja=");b[n++]=s->armed?'1':'0';n+=tel_s(b+n," jv=");b[n++]=s->valid?'1':'0';
  {u64 g=r->last_fresh && now>=r->last_fresh?(now-r->last_fresh)/(BSP_CLINT_HZ/1000u):99999u;n+=tel_s(b+n," jg=");n+=tel_u(b+n,g>99999u?99999u:(uint32_t)g);}
  n+=tel_s(b+n," jp=");n+=tel_u(b+n,s->pairs-tel_jp);
  n+=tel_s(b+n," jr=");n+=tel_u(b+n,r->attempts-tel_ja);b[n++]='/';n+=tel_u(b+n,r->ok-tel_jo);b[n++]='/';n+=tel_u(b+n,r->fail-tel_jx);b[n++]='/';
  n+=tel_u(b+n,r->last_ticks/(BSP_CLINT_HZ/1000u));n+=tel_s(b+n," jb=");n+=tel_u(b+n,r->bus_recovers-tel_jb);
  n+=tel_s(b+n," js=");n+=tel_u(b+n,r->calls-tel_js);n+=tel_s(b+n," jx=");n+=tel_u(b+n,joystick_take_gap_window()/(BSP_CLINT_HZ/1000000u));
  n+=tel_s(b+n," jn=");n+=tel_u(b+n,r->pair_starts-tel_jn);n+=tel_s(b+n," rp=");n+=tel_u(b+n,t.pmax_us);tel_joy_mark();}
 if(tel_drop){n+=tel_s(b+n," dr=");n+=tel_u(b+n,tel_drop);}
 n+=tel_s(b+n,"\r\n");tel_commit(b,n);
 /* Read-only, once-per-second evidence from ADC pair through consumed input
  * to displayed position. Same bounded, nonblocking TX ring as TEL. */
 {const joystick_status_t *s=joystick_status();const joystick_recovery_t *r=joystick_recovery();
  n=tel_s(b,"JOY seq=");n+=tel_u(b+n,s->sequence);
  n+=tel_s(b+n," raw=");n+=tel_i(b+n,s->raw[0]);b[n++]='/';n+=tel_i(b+n,s->raw[1]);
  n+=tel_s(b+n," mask=");n+=tel_u(b+n,player_input_mask());
  n+=tel_s(b+n," xy=");n+=tel_u(b+n,player_scene_displayed_x());b[n++]='/';n+=tel_u(b+n,player_scene_displayed_y());
  n+=tel_s(b+n," cap=");n+=tel_u(b+n,r->consecutive);b[n++]='/';n+=tel_u(b+n,r->exhausted);
  n+=tel_s(b+n," bok=");n+=tel_u(b+n,r->bus_recover_ok);
  n+=tel_s(b+n,"\r\n");tel_commit(b,n);}
}
static void game_poll(void){
 u64 now=clint_getTime(BSP_CLINT);
 int guard=renderer_cpu_sync_waiting();
 enemy_test_input_service();
 enemy_test_service(guard);
 if(!guard)enemy_test_app_service(game_lifecycle_busy() || asset_upload_active());
 game_hud_service();
 if(!serial_closed){serial_poll(guard);if(enemy_test_app_started() && !asset_upload_active())serial_closed=1;}
 if(!guard){menu_service(now);status_service(now);decor_scene_pressure_glow(bullet_scene_pressure_glow());tel_service(now);tel_pump();} /* GHDA */
}
static void single_init(void){
 const enemy_test_hooks_t hooks={game_prepare_surface,game_surface_snapshot,game_request_idle,game_idle_confirmed};
 const u32 centre[3]={GAME_JOY_CENTER_X,GAME_JOY_CENTER_Y,GAME_JOY_INVERT};
 bsp_init();tier_tags_init();enemy_test_init(&hooks);asset_upload_init();
 asset_upload_set_hooks(game_asset_prepare,game_asset_resume);
 (void)joystick_center(clint_getTime(BSP_CLINT),centre);
 renderer_cpu_set_target(GPU_TARGET_FULL_CPU);renderer_cpu_set_service(game_poll);
 game_lifecycle_init(game_poll);
 uart_writeStr(BSP_UART_TERMINAL,"\r\nBUILD: " BUILD_ID " single ELF (asset upload + game); waiting for confirmed idle\r\n");
}
void main(void){
 single_init();
 for(;;){game_poll();game_lifecycle_service();}
}
#endif
