#ifndef COMBAT_H
#define COMBAT_H
#include "gpu.h"
/* Half-open rectangles. During a continuous step both corners move linearly
 * from (previous_x0,previous_y0) to (x0,y0); size is constant. A new birth,
 * wrap or respawn has continuous=0: compare endpoints only. */
typedef struct { int32_t x0,y0,x1,y1,previous_x0,previous_y0;u32 continuous; } combat_box_t;
/* Exact relative 2D sweep: 1 and first-contact time n/d (0<=n<=d, d>0) when the
 * open rectangles overlap at some t in [0,1]; a touch without overlap is a miss. */
int combat_contact(const combat_box_t *a,const combat_box_t *b,u32 *n,u32 *d);
u32 combat_displayed_pb_kills(void);
void combat_init(void);
void combat_begin_frame(void);
void combat_resolve(u64 now);
void combat_claimed(void);   /* S: snapshot at claim (PIPE ON only) */
void combat_confirmed(void);
void combat_restored(void);
u32 combat_displayed_hits(void);
u32 combat_displayed_kills(void);
u32 combat_displayed_player_hits(void);
u32 combat_displayed_ticks(void);
u32 combat_displayed_max_ticks(void);
#endif
