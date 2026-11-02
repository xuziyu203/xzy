#include "bullet_stats.h"
#include <string.h>
/* GHD: see bullet_stats.h. Pure bookkeeping (no I/O), a few adds per shown frame. */
static bullet_stats_t shown;
static bullet_stats_window_t window;

void bullet_stats_window_reset(void){memset(&window,0,sizeof(window));}
void bullet_stats_reset(void){memset(&shown,0,sizeof(shown));bullet_stats_window_reset();}
void bullet_stats_confirmed(uint32_t air,uint32_t missile,uint32_t turret,uint32_t player,uint32_t groups){
 shown.air=air;shown.missile=missile;shown.turret=turret;shown.player=player;shown.groups=groups;
 shown.enemy=air+missile+turret;shown.total=shown.enemy+player;
 if(!window.frames || shown.total<window.total_min)window.total_min=shown.total;
 if(shown.total>window.total_max)window.total_max=shown.total;
 if(air>window.air_max)window.air_max=air;
 if(turret>window.turret_max)window.turret_max=turret;
 window.total_sum+=shown.total;window.air_sum+=air;window.missile_sum+=missile;window.turret_sum+=turret;window.player_sum+=player;
 window.frames++;
}
const bullet_stats_t *bullet_stats_displayed(void){return &shown;}
const bullet_stats_window_t *bullet_stats_window(void){return &window;}
