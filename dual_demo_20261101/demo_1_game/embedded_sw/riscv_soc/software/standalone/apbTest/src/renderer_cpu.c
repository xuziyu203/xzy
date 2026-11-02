#include "renderer.h"
#include "cpu_writer.h"
#include "game_lifecycle.h"
#include "bsp.h"
#include "io.h"
#include <string.h>
static uint16_t cpu_row[CPU_FULL_WIDTH_PIXELS];
static uint8_t cpu_row_leased,cpu_sync_waiting,cpu_seed_aborting;
static uint32_t cpu_row_borrow_block;
static void (*cpu_service)(void);
void renderer_cpu_set_target(uint32_t target){(void)target;cpu_writer_set_target(CPU_FULL_TARGET);}
void renderer_cpu_set_service(void (*service)(void)){cpu_service=service;}
int renderer_cpu_sync_waiting(void){return cpu_sync_waiting!=0;}
int renderer_cpu_row_borrow(uint16_t **row){
 cpu_row_borrow_block=0;
 if(!row)cpu_row_borrow_block|=RENDERER_CPU_ROW_BORROW_PARAM;
 if(cpu_row_leased || cpu_sync_waiting)cpu_row_borrow_block|=RENDERER_CPU_ROW_BORROW_LEASED;
 if(cpu_writer_status()&(CPU_WR_STATUS_FRAME_ACTIVE|CPU_WR_STATUS_ROW_ACTIVE|CPU_WR_STATUS_DDR_INFLIGHT))cpu_row_borrow_block|=RENDERER_CPU_ROW_BORROW_ASYNC;
 if(cpu_row_borrow_block)return -1;
 cpu_row_leased=1;*row=cpu_row;return 0;
}
uint32_t renderer_cpu_row_borrow_block(void){return cpu_row_borrow_block;}
void renderer_cpu_row_release(void){if(!cpu_seed_aborting)cpu_row_leased=0;}
void renderer_cpu_seed_drain(void){
 if(cpu_seed_aborting && !(cpu_writer_status()&(CPU_WR_STATUS_FRAME_ACTIVE|CPU_WR_STATUS_ROW_ACTIVE|CPU_WR_STATUS_DDR_INFLIGHT))){
  cpu_seed_aborting=0;cpu_row_leased=0;
 }
}
int renderer_cpu_seed_black(void (*poll)(void)){
 uint16_t *row;int rc;uint32_t y;
 if(renderer_cpu_row_borrow(&row))return -1;
 memset(row,0,sizeof(cpu_row));cpu_sync_waiting=1;
 cpu_writer_set_target(CPU_FULL_TARGET);
 rc=cpu_writer_begin_frame(poll?poll:cpu_service);
 for(y=0;!rc && y<CPU_FULL_HEIGHT_PIXELS;y++)rc=cpu_writer_write_row(0,y,(const uint8_t*)row,CPU_FULL_WIDTH_PIXELS,poll?poll:cpu_service);
 if(!rc)rc=cpu_writer_end_frame(poll?poll:cpu_service);
 if(rc){
  /* Abort requests drain, never discards an outstanding AXI write response. */
  cpu_seed_aborting=1;write_u32(CPU_WR_ABORT,CPU_WR_CONTROL);
 }else cpu_row_leased=0;
 cpu_sync_waiting=0;return rc;
}
