#ifndef JOYSTICK_ADS1115_H
#define JOYSTICK_ADS1115_H
#include "player_input.h"
typedef struct {
 u64 issue[2],ready[2],done[2],read[2],last_service;
 u32 sequence,epoch,pairs,faults,error,probes,max_gap,max_service_ticks,max_mmio,max_steps;
 int16_t raw[2];
 uint8_t enabled,armed,calibrated,latched,valid;
 /* CMDJ (STATUS ABI 3, appended words 31..35). age = publish - issue[0]; cover = publish - issue[0] of the previous published
  * pair in the same input epoch. late: age >= 10 ms (the former PAIR_TIMEOUT condition, counted, not a fault); stale: age >=
  * HZ/25 (player_input freshness window), never handed to player_input. age_max/cover_max restart at joystick_enable(). */
 u32 late,stale,age_max,cover_max,restart_recoveries;
} joystick_status_t;
/* Separate, read-only diagnostic ABI. All fields are 32-bit wire words. */
enum { JOY_DIAG_CLASS_COUNT=20 };
typedef struct {
 u32 valid,epoch,error,reason,now_lo,now_hi;
 u32 state,ts,pos,kind,axis,service_gap,timing_valid;
 u32 transaction_elapsed,byte_elapsed,pair_elapsed;
 /* CSR order: 0x40, 0x44, 0x04, 0x08, 0x0c. Age uses service-entry time. */
 u32 csr_valid,csr_value[5],csr_point[5],csr_age[5];
 u32 permission_stage,permission_data,permission_we,permission_data_write,permission_we_write;
} joystick_diag_fault_t;
typedef struct {
 u32 abi,retry_epoch,count[JOY_DIAG_CLASS_COUNT];
 joystick_diag_fault_t first[2];
} joystick_diag_t;
void joystick_init(void);
void joystick_service(u64 now);
void joystick_enable(u64 now,int enable);
int joystick_center(u64 now,const u32 value[3]);
const joystick_status_t *joystick_status(void);
const joystick_diag_t *joystick_diag(void);
void joystick_retry(u64 now);
void joystick_restart_recover(u64 now); /* CMDJ: APP KEY3 start/restart; acts only on a latched, enabled driver */
/* GHDA automatic recovery of a latched driver (see joystick_ads1115.c): counters and durations in CLINT ticks. */
typedef struct { u32 latches,attempts,ok,fail,active,latch_seen,last_ticks,max_ticks,bus_recovers,bus_recover_ok;
                 u32 calls,pair_starts,gap_win_max; /* GHDA step 6: service calls, pair attempts started, max service gap (ticks) since the last window take */
                 u64 latch_since,t0,next,last_fresh; u32 consecutive,exhausted; } joystick_recovery_t;
u32 joystick_take_gap_window(void); /* GHDA: returns the max service gap (ticks) since the previous call and restarts the window */
const joystick_recovery_t *joystick_recovery(void);
#endif
