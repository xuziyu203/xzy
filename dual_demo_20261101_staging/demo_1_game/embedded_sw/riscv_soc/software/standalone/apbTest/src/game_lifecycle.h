#ifndef GAME_LIFECYCLE_H
#define GAME_LIFECYCLE_H
#include <stdint.h>
#define GAME_ONLY_ABI 0x474f0200u
#define GAME_ONLY_ABI_OFFSET 0x060u
#define GAME_ONLY_HANDOFF_OFFSET 0x090u
#define GAME_ONLY_INIT_DONE 4u
#define GAME_ONLY_READ_ERROR 8u
#define GAME_ONLY_BUSY_MASK 0x1fbu
enum { GO_WAIT=1, GO_SEED, GO_ACK, GO_IDLE, GO_FAILED, GO_GAME };
void game_lifecycle_init(void (*poll)(void));
void game_lifecycle_service(void);
int game_lifecycle_busy(void);
int game_lifecycle_safe(void);
uint32_t game_idle_epoch(void);
uint32_t game_idle_generation(uint32_t epoch);
int game_prepare_surface(void);
void game_surface_snapshot(uint32_t *mode,uint32_t *source);
int game_request_idle(uint32_t mode,uint32_t source);
int game_idle_confirmed(void);
int game_inactive_exit(void);
int game_asset_prepare(void);
void game_asset_resume(void);
int game_bg_prepare(void);
int game_bg_restore(void);
int game_bg_restore_status(void);
int game_bg_restore_pending(void);
void game_lifecycle_status(void);
int renderer_cpu_seed_black(void (*poll)(void));
void renderer_cpu_seed_drain(void);
#endif
