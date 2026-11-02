#ifndef ENEMY_TEST_H
#define ENEMY_TEST_H
#include <stdint.h>
#define ENEMY_ABI 0x454e0101u
#define ENEMY_STATIC_ABI 0x454e0100u
#define ENEMY_REG_ABI 0x160u
#define ENEMY_REG_CONTROL 0x164u
#define ENEMY_REG_STATUS 0x168u
#define ENEMY_REG_ERROR 0x16cu
#define ENEMY_REG_BACK 0x170u
#define ENEMY_REG_BYTES 0x174u
#define ENEMY_REG_PHASE 0x178u
#define ENEMY_REG_Y 0x180u
typedef struct {
 int (*prepare)(void);
 void (*snapshot)(uint32_t *mode,uint32_t *source);
 int (*restore)(uint32_t mode,uint32_t source);
 int (*restored)(void); /* 1 only after an actual completed restore frame */
} enemy_test_hooks_t;
void enemy_test_init(const enemy_test_hooks_t *hooks);
/* -1: no reply reservation; caller retains the command and retries later. */
int enemy_test_handle_line(const char *line,int peer_busy);
/* Only the UART dispatcher may select this for an active BG/ASSET protocol. */
int enemy_test_handle_protocol_line(const char *line,int peer_busy);
/* A KEY rejection at a protocol text boundary uses the existing reply queue.
 * Pause protocol service/RX until this bounded response is fully enqueued to TX. */
int enemy_test_protocol_reply_pending(void);
int enemy_test_service_protocol_reply(void);
void enemy_test_service(int resource_guard);
void enemy_test_input_service(void);
/* CMD standalone application (start menu, keys, FLOW); main loop only, never in the writer-wait callback. */
void enemy_test_app_service(int peer_busy);
int enemy_test_input_line(const char *line);
int enemy_test_active(void);
int enemy_test_input_moving_public(void); /* GHD: player input accepted now (a game round is moving) */
int enemy_test_app_in_menu(void); /* GHD: the start menu is shown */
int enemy_test_app_started(void); /* GHD3S: a game was started at least once since reset */
void enemy_test_frame_metrics(uint32_t v[4]);
uint32_t enemy_test_transition_us(void); /* GHD4: last light->pressure transition, us (not part of L/X) */ /* GHD3S: intervals > 25 ms, longest interval us, longest compose us, longest critical prep us */
int enemy_test_blocks_scene(void);
/* Stage-1 diagnostics (D): observation only, never alters scheduling. */
enum { EN_DIAG_DEFERRED, EN_DIAG_TIMEOUT, EN_DIAG_PRIORITY, EN_DIAG_FLOOR };
int enemy_test_is_heavy_query(const char *line); /* read-only query, line without leading spaces */
int enemy_test_is_priority_line(const char *line); /* exact ENEMY STOP / ENEMY EXIT */
void enemy_test_diag_command(unsigned heavy,uint64_t ready_at,uint64_t start,uint64_t end,unsigned queued_before,unsigned queued_after);
void enemy_test_diag_event(unsigned kind,uint32_t value);
void enemy_test_diag_tx_poll(void);
/* O2b: 1 while table staging (before the first claim attempt), after the
 * composed frame waits for VSYNC/confirmation, or between confirmation and
 * the next start; 0 in every stopped, failing or exiting state. */
int enemy_test_query_busy(void);
/* GHDA telemetry: shown-frame accumulators since the last take (frames, frames with an interval > 25 ms, max interval / compose (C) /
 * preparation (R) us, real bullets (bullet_stats total) sum/min/max, groups sum/max, glow (decor) sum/max, stress phase). */
typedef struct { uint32_t frames,long_frames,imax_us,cmax_us,rmax_us,bul_sum,bul_min,bul_max,grp_sum,grp_max,glow_sum,glow_max,stress,pmax_us; } enemy_test_telem_t;
void enemy_test_telem_take(enemy_test_telem_t *t);
#endif
