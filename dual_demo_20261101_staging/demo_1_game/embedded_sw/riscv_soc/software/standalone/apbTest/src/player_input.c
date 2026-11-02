#include "player_input.h"
#include "bsp.h"
#include <string.h>

/* This is displacement intent, never a coordinate or a direction history.
 * All timestamps are complete-line CLINT samples from the sole UART reader. */
static u32 input_generation,input_session,input_sequence,input_sequence_valid,input_mask;
static u64 input_last_new,input_cursor;
static int64_t input_dx,input_dy,input_rx,input_ry;
static int64_t joy_dx,joy_dy;
static u64 joy_issue;
static u32 joy_epoch=1,joy_seq,joy_mask,joy_valid,joy_permit,input_owner,local_fire;
static __attribute__((noinline)) u32 input_effective(u64 now,u32 *owner){
 u32 uart=input_session && input_sequence_valid && now>=input_last_new && now-input_last_new<PLAYER_INPUT_TIMEOUT_TICKS?input_mask:0;
 u32 joy=joy_permit && joy_valid && now>=joy_issue && now-joy_issue<(u64)BSP_CLINT_HZ/25u?joy_mask:0;
 u32 chosen=(!(uart&15u))?1u:0u;
 if(owner)*owner=chosen;
 return (chosen?joy:(uart&15u))|(uart&48u)|(joy_permit?local_fire:0);
}
static void input_clear(u64 now){
 input_mask=0;input_dx=input_dy=joy_dx=joy_dy=input_rx=input_ry=0;input_cursor=now;
}
void player_input_init(void){input_generation=input_session=input_sequence=input_sequence_valid=0;input_last_new=0;local_fire=joy_permit=joy_valid=0;joy_epoch=1;input_clear(0);}
void player_input_local_fault(u64 now){joy_valid=joy_mask=joy_seq=0;joy_dx=joy_dy=0;if(input_owner)input_rx=input_ry=0;input_cursor=now;if(++joy_epoch==0)++joy_epoch;}
u32 player_input_local_epoch(void){return joy_epoch;}
void player_input_revoke(u64 now){input_session=input_sequence=input_sequence_valid=0;input_last_new=now;joy_permit=local_fire=0;player_input_local_fault(now);input_clear(now);}
u32 player_input_connect(u64 now){
 player_input_service(now,1);input_session=input_sequence=input_sequence_valid=input_mask=0;input_dx=input_dy=0;if(++input_generation==0)++input_generation;
 input_session=input_generation;return input_session;
}
u32 player_input_mask(void){return input_effective(input_cursor,0);}
u32 player_input_session(void){return input_session;}
int player_input_is_data(const char *s){return strncmp(s,"PLAYER INPUT",12)==0 && (s[12]==0 || s[12]==' ');}
static int input_hex(const char *s,unsigned n,u32 *v){
 unsigned i;u32 value=0;
 for(i=0;i<n;i++){
  unsigned c=(unsigned char)s[i],d;
  if(c>='0' && c<='9')d=c-'0';else if(c>='A' && c<='F')d=c-'A'+10u;
  else if(c>='a' && c<='f')d=c-'a'+10u;else return 0;
  value=value*16u+d;
 }
 *v=value;return 1;
}
static __attribute__((noinline)) void input_velocity(u32 mask,int32_t *vx,int32_t *vy){
 int x=((mask&8u)!=0)-((mask&2u)!=0),y=((mask&4u)!=0)-((mask&1u)!=0);
 int32_t speed=(mask&16u)?7864320:19660800;
 if(x && y)speed=(mask&16u)?5560914:13902285;
 *vx=x*speed;*vy=y*speed;
}
void player_input_service(u64 now,int moving){
 int32_t vx,vy;int64_t scaled;u64 dt;
 if(!moving){input_clear(now);return;}
 if(now<input_cursor || (input_sequence_valid && now<input_last_new)){
  input_clear(now);input_last_new=now;return;
 }
 /* Expiry takes priority: no movement can be caught up after a stalled poll. */
 if(input_session && input_sequence_valid && now-input_last_new>=PLAYER_INPUT_TIMEOUT_TICKS){input_mask=0;input_dx=input_dy=0;if(!input_owner){input_rx=input_ry=0;input_cursor=now;}}
 if(joy_valid && (now<joy_issue || now-joy_issue>=(u64)BSP_CLINT_HZ/25u)){joy_valid=0;joy_dx=joy_dy=0;if(input_owner){input_rx=input_ry=0;input_cursor=now;}}
 {u32 old=input_owner,mask=input_effective(now,&input_owner);if(old!=input_owner)input_rx=input_ry=0;dt=now-input_cursor;input_cursor=now;input_velocity(mask,&vx,&vy);}
 scaled=(int64_t)vx*(int64_t)dt+input_rx;if(input_owner)joy_dx+=scaled/BSP_CLINT_HZ;else input_dx+=scaled/BSP_CLINT_HZ;input_rx=scaled%BSP_CLINT_HZ;
 scaled=(int64_t)vy*(int64_t)dt+input_ry;if(input_owner)joy_dy+=scaled/BSP_CLINT_HZ;else input_dy+=scaled/BSP_CLINT_HZ;input_ry=scaled%BSP_CLINT_HZ;
}
void player_input_local_gate(u64 now,int permit){if(joy_permit!=(u32)(permit!=0)){player_input_service(now,1);joy_permit=permit!=0;player_input_local_fault(now);}}
void player_input_local_fire(int held){local_fire=held?32u:0u;}
void player_input_local_pair(u64 now,u64 issue,u32 epoch,u32 seq,u32 mask){
 int32_t vx,vy,oldvx,oldvy;
 if(epoch!=joy_epoch || !joy_permit || (mask&~15u) || now<issue || now-issue>=(u64)BSP_CLINT_HZ/25u || (joy_seq && (seq-joy_seq==0 || seq-joy_seq>=0x80000000u)))return;
 player_input_service(now,1);input_velocity(input_effective(now,0),&oldvx,&oldvy);
 joy_seq=seq;joy_issue=issue;joy_mask=mask;joy_valid=1;
 input_velocity(input_effective(now,0),&vx,&vy);if(vx!=oldvx)input_rx=0;if(vy!=oldvy)input_ry=0;
}
void player_input_line(const char *s,u64 now){
 u32 session,sequence,mask,delta;int32_t vx,vy,oldvx,oldvy;
 if(strlen(s)!=33u || strncmp(s,"PLAYER INPUT ",13)!=0 || s[21]!=' ' || s[30]!=' ' ||
    !input_hex(s+13,8,&session) || !input_hex(s+22,8,&sequence) || !input_hex(s+31,2,&mask) || (mask&~63u))return;
 if(!session || session!=input_session)return;
 if(!input_sequence_valid){if(mask)return;}
 else{delta=sequence-input_sequence;if(!delta || delta>=0x80000000u)return;}
 player_input_service(now,1);input_velocity(input_effective(now,0),&oldvx,&oldvy);
 input_sequence=sequence;input_sequence_valid=1;input_mask=mask;input_last_new=input_cursor=now;
 input_velocity(input_effective(now,0),&vx,&vy);
 if(vx!=oldvx)input_rx=0;
 if(vy!=oldvy)input_ry=0;
}
void player_input_take(int64_t *dx,int64_t *dy){*dx=input_dx+joy_dx;*dy=input_dy+joy_dy;input_dx=input_dy=joy_dx=joy_dy=0;}
void player_input_clip(int clip_x,int clip_y){if(clip_x)input_rx=0;if(clip_y)input_ry=0;}
