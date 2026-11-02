#ifndef BUILD_VARIANT_GAME /* GHD3: full UART build only (single ELF: game_noserial.c stubs) */
#include "uart_reply.h"
#include "bsp.h"
#include "io.h"
#include <string.h>

/* Host MMIO fixtures override only this single byte store. */
#ifndef UART_REPLY_WRITE_BYTE
#define UART_REPLY_WRITE_BYTE(c) write_u32((uint32_t)(unsigned char)(c), BSP_UART_TERMINAL + UART_DATA)
#endif

/* Single main-loop producer/consumer. No allocator, ISR or renderer callback. */
static char reply_bytes[UART_REPLY_CAPACITY];
static unsigned reply_head,reply_count,reply_building,reply_length,reply_limit;
static unsigned reply_overflow;
static uint32_t reply_sent_total;
static char reply_hold[UART_REPLY_HOLD_CAPACITY];
static unsigned reply_hold_on,reply_hold_len,reply_hold_building,reply_direct;

int uart_reply_pending(void){return reply_count!=0u || reply_hold_len!=0u;}
unsigned uart_reply_queued(void){return reply_count;}
uint32_t uart_reply_sent_total(void){return reply_sent_total;}
int uart_reply_query_ready(void){
 return UART_REPLY_CAPACITY-reply_count>=UART_REPLY_QUERY_MAX+UART_REPLY_EVENT_RESERVE;
}
int uart_reply_begin(unsigned limit){
 if(reply_building)return 0;
 if(reply_hold_on && !reply_direct){
  unsigned room=UART_REPLY_HOLD_CAPACITY-reply_hold_len;
  if(limit>room){if(room<UART_REPLY_HOLD_MIN)return 0;limit=room;}
  reply_hold_building=1;reply_building=1;reply_length=0;reply_limit=limit;reply_overflow=0;return 1;
 }
 if(limit>UART_REPLY_CAPACITY-reply_count)return 0;
 reply_building=1;reply_length=0;reply_limit=limit;reply_overflow=0;return 1;
}
void uart_reply_hold_enter(void){reply_hold_on=1u;}
int uart_reply_holding(void){return reply_hold_on!=0u;}
int uart_reply_hold_room(void){return !reply_building && UART_REPLY_HOLD_CAPACITY-reply_hold_len>=UART_REPLY_HOLD_MIN;}
unsigned uart_reply_hold_bytes(void){return reply_hold_len;}
void uart_reply_direct(unsigned on){reply_direct=on;}
int uart_reply_hold_release(void){
 unsigned i;
 if(!reply_hold_on)return 1;
 if(reply_building || UART_REPLY_CAPACITY-reply_count<reply_hold_len)return 0;
 for(i=0;i<reply_hold_len;i++)reply_bytes[(reply_head+reply_count+i)%UART_REPLY_CAPACITY]=reply_hold[i];
 reply_count+=reply_hold_len;reply_hold_len=0;reply_hold_on=0;return 1;
}
void uart_reply_write(uint32_t port,char value){
 if(!reply_building){uart_write(port,value);return;}
 if(reply_length>=reply_limit)reply_overflow=1;
 else if(reply_hold_building)reply_hold[reply_hold_len+reply_length++]=value;
 else reply_bytes[(reply_head+reply_count+reply_length++)%UART_REPLY_CAPACITY]=value;
}
void uart_reply_writeStr(uint32_t port,const char *text){
 if(!reply_building){uart_writeStr(port,text);return;}
 while(*text)uart_reply_write(port,*text++);
}
void uart_reply_writeHex(uint32_t port,int value){
 static const char hex[]="0123456789ABCDEF";unsigned n;
 if(!reply_building){uart_writeHex(port,value);return;}
 for(n=8;n!=0;n--)uart_reply_write(port,hex[((uint32_t)value>>((n-1)*4))&15]);
}
void uart_reply_commit(void){
 static const char error[]="ERR RESPONSE_OVERFLOW\r\n";unsigned i;
 if(!reply_building)return;
 if(reply_hold_building){
  if(reply_overflow)for(reply_length=0;reply_length<sizeof(error)-1 && reply_length<reply_limit;reply_length++)
   reply_hold[reply_hold_len+reply_length]=error[reply_length];
  reply_hold_len+=reply_length;reply_building=reply_hold_building=0;return;
 }
 if(reply_overflow){
  /* Never publish a truncated status line or touch outside its reservation. */
  reply_length=0;
  for(i=0;i<sizeof(error)-1 && i<reply_limit;i++)
   reply_bytes[(reply_head+reply_count+reply_length++)%UART_REPLY_CAPACITY]=error[i];
 }
 reply_count+=reply_length;reply_building=0;
}
void uart_reply_event(const char *text){
 if(reply_building){uart_reply_writeStr(BSP_UART_TERMINAL,text);return;}
 /* Admission leaves 256 bytes for the finite SHOWN/STOP/EXIT/failure chain.
  * Service emits each such event once; commands cannot consume this reserve. */
 if(uart_reply_begin((unsigned)strlen(text))){
  uart_reply_writeStr(BSP_UART_TERMINAL,text);uart_reply_commit();
 }
}
void uart_reply_service(void){
 unsigned budget=UART_REPLY_SLICE;
 while(budget && reply_count){
  if(uart_writeAvailability(BSP_UART_TERMINAL)==0u)break;
  /* Single UART producer: one available slot, one store, never a TX wait. */
  UART_REPLY_WRITE_BYTE(reply_bytes[reply_head]);
  reply_head=(reply_head+1u)%UART_REPLY_CAPACITY;reply_count--;budget--;reply_sent_total++;
 }
}
#endif
