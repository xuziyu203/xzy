#include "game_hud.h"
#include "gpu.h"
#include "bsp.h"
#include "io.h"
#include "build_id.h"
#include <string.h>

static unsigned hud_active, hud2, hud_scene_valid, hud_dirty, hud_release, hud_enabled;
static uint32_t hud_frames, hud_base_frames, hud_objects, hud_fps;
static u64 hud_window_start;
static gpu_panel_snapshot_t hud_latest;
static uint32_t hud_game_enabled, hud_lives, hud_score, hud_over;
static uint32_t hud_enemy, hud_bullets, hud_shield; /* GHD HUD2 fields (hud_bullets = actual_count) */
static unsigned hud_mode;                        /* GHD8: GAME_HUD_MODE_LIGHT / _STRESS of the shown frame (MODE line) */
static uint32_t hud_display;                     /* GHD9: BULLETS display count, recomputed from hud_bullets every update */
static char hud_tag[6] = BUILD_HUD_TAG; /* GHD: BUILD field; game ELF shows status codes here */
static unsigned hud_status; /* GHD: status panel (text on, game fields blank) while no game rules run */

static uint32_t hud_row(uint32_t objects, uint32_t fps) {
    if (objects > 9999u) objects = 9999u;
    if (fps > 999u) fps = 999u;
    return GPU_PANEL_ROW_FPS_VALID | (objects % 10u) | (((objects / 10u) % 10u) << 4) |
           ((fps % 10u) << 8) | (((fps / 10u) % 10u) << 12) | ((fps / 100u) << 16);
}
/* GHD HUD2: n BCD digits of min(v, 10^n-1), most significant digit in the highest nibble; with blank, leading zeros become
 * 0xF (blank) and a zero value keeps one '0'. Mirrors hud_design.bcd_nibbles. */
static uint32_t hud2_bcd(uint32_t v, unsigned n, unsigned blank) {
    uint32_t lim = 1u, w = 0u, d[6];
    unsigned i;
    for (i = 0u; i < n; i++) lim *= 10u;
    if (v > lim - 1u) v = lim - 1u;
    for (i = n; i > 0u; i--) { d[i - 1u] = v % 10u; v /= 10u; }
    if (blank) for (i = 0u; i + 1u < n && d[i] == 0u; i++) d[i] = 0xFu;
    for (i = 0u; i < n; i++) w = (w << 4) | d[i];
    return w;
}
static uint32_t hud2_tag(const char *s) {
    static const char chars[] = GPU_HUD2_CHARS;
    uint32_t w = 0u;
    unsigned k, c;
    for (k = 0u; k < 5u && s[k]; k++) {
        for (c = 0u; chars[c] && chars[c] != s[k]; c++) {}
        w |= (uint32_t)(chars[c] ? c : 0u) << (6u * k);
    }
    return w;
}
static void hud_encode(void) {
    memset(&hud_latest, 0, sizeof(hud_latest));
    if (hud2) {
        uint32_t hp = hud_lives > 10u ? 10u : hud_lives;
        if (!hud_enabled) return; /* flags 0: HUD2 and the q3 game HUD both off */
        hud_latest.flags = GPU_PANEL_FLAG_HUD2 | GPU_PANEL_FLAG_FULL_HUD_VALID;
        hud_latest.cpu_row = hud2_bcd(hud_fps, 3u, 1u) | (hud2_bcd(hud_enemy, 3u, 1u) << 16);
        hud_latest.blit_row = hud2_bcd(hud_bullets, 4u, 1u) |
            ((hud_game_enabled ? (hud2_bcd(hp, 2u, 1u) | (hp << 8)) : 0x0FFu) << 16) |
            (hud_shield ? 1u << 28 : 0u) | (hud_over ? 1u << 29 : 0u) | ((hud_game_enabled || hud_status) ? 1u << 30 : 0u);
        hud_latest.rect_row = hud2_bcd(hud_score, 6u, 0u);
        hud_latest.blit_time = hud2_tag(hud_tag);
        hud_latest.rect_time = (hud2_bcd(hud_display, 4u, 1u) << 16) | (hud_game_enabled ? (hud_mode & 3u) : 0u); /* GHD9: w4 = BULLETS | MODE */
        return;
    }
    {
        uint32_t objects = hud_objects > 9999u ? 9999u : hud_objects;
        hud_latest.flags = GPU_PANEL_FLAG_GAME_HUD |
            (hud_enabled ? GPU_PANEL_FLAG_FULL_HUD_VALID : 0u);
        hud_latest.cpu_row = hud_row(objects, hud_fps);
        /* HUD4: BLIT[11:8] thousands, BLIT[7:4] hundreds (lives stay in [3:0]). */
        hud_latest.blit_row = ((objects / 1000u) << 8) | (((objects / 100u) % 10u) << 4);
        if (hud_enabled && hud_game_enabled) {
            uint32_t s = hud_score > 999999u ? 999999u : hud_score, bcd = 0u, k;
            for (k = 0u; k < 6u; k++) { bcd |= (s % 10u) << (4u * k); s /= 10u; }
            hud_latest.flags |= GPU_PANEL_FLAG_GAME_RULES | (hud_over ? GPU_PANEL_FLAG_GAME_OVER : 0u);
            hud_latest.blit_row |= hud_lives > 9u ? 9u : hud_lives;
            hud_latest.rect_row = bcd;
        }
    }
}
static void hud_snapshot(unsigned enabled) {
    hud_enabled = enabled;
    hud_encode();
    hud_dirty = 1u;
}
void game_hud_begin(void) {
    /* Old bit remains usable: never enable a flag it does not implement. */
    uint32_t abi = read_u32(GPU_REG_GAME_HUD_ABI);
    hud2 = abi == GPU_GAME_HUD2_ABI;
    hud_active = hud2 || abi == GPU_GAME_HUD_ABI;
    hud_scene_valid = hud_dirty = hud_release = hud_enabled = hud_status = 0u;
    hud_game_enabled = hud_lives = hud_score = hud_over = 0u;
    hud_enemy = hud_bullets = hud_shield = 0u;
    hud_mode = GAME_HUD_MODE_NONE; hud_display = 0u;
    hud_frames = hud_base_frames = hud_objects = hud_fps = 0u;
    hud_window_start = clint_getTime(BSP_CLINT);
}
void game_hud_confirmed(uint32_t completed_frames, uint32_t visible_objects) {
    unsigned reset;
    if (!hud_active) return;
    reset = completed_frames < hud_frames;
    if (reset) {
        hud_base_frames = completed_frames;
        hud_window_start = clint_getTime(BSP_CLINT);
        hud_fps = 0u;
    }
    hud_frames = completed_frames;
    if (!hud_scene_valid || reset || (!hud2 && hud_objects != visible_objects)) {
        hud_objects = visible_objects;
        hud_scene_valid = 1u;
        hud_snapshot(1u);
    }
}
void game_hud_set_game(uint32_t enabled, uint32_t lives, uint32_t score, uint32_t over) {
    if (!hud_active) return;
    if (enabled == hud_game_enabled && lives == hud_lives && score == hud_score && over == hud_over) return;
    hud_game_enabled = enabled; hud_lives = lives; hud_score = score; hud_over = over;
    if (hud_scene_valid) hud_snapshot(1u);
}
unsigned game_hud_mode_displayed(void) { return hud_mode; }
uint32_t game_hud_display_count(uint32_t actual, unsigned stress) {
    uint64_t v;
    if (!stress || actual <= 1000u) return actual;
    v = (uint64_t)actual + 400u;   /* GHDA: exact real + 400 (ROUND100 removed) */
    return v > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)v;
}
uint32_t game_hud_display_displayed(void) { return hud_display; }
void game_hud_set_hud2(uint32_t enemy, uint32_t bullets, uint32_t shield, unsigned mode) {
    if (!hud_active || !hud2) return;
    shield = shield != 0u; mode = mode == GAME_HUD_MODE_STRESS ? GAME_HUD_MODE_STRESS : GAME_HUD_MODE_LIGHT;
    if (enemy == hud_enemy && bullets == hud_bullets && shield == hud_shield && mode == hud_mode) return;
    hud_enemy = enemy; hud_bullets = bullets; hud_shield = shield; hud_mode = mode;
    hud_display = game_hud_display_count(bullets, mode == GAME_HUD_MODE_STRESS); /* same snapshot as MODE, never accumulated */
    if (hud_scene_valid) hud_snapshot(1u);
}
int game_hud_is_hud2(void) { return hud_active && hud2; }
/* GHD: 5-character code in the BUILD field (characters of GPU_HUD2_CHARS); NULL restores the build tag. Published with the next
 * snapshot. game_hud_status_panel shows the panel (frame, 1080P line, BUILD field) without game rules while no game HUD runs. */
void game_hud_set_tag(const char *tag) {
    unsigned k, end = 0u; const char *s = tag ? tag : BUILD_HUD_TAG;
    for (k = 0u; k < 5u; k++) { if (!end && !s[k]) end = 1u; hud_tag[k] = end ? ' ' : s[k]; }
    hud_tag[5] = 0;
    if (hud_active && hud_scene_valid) hud_snapshot(1u);
}
void game_hud_status_panel(void) {
    if (hud_active) return;
    game_hud_begin();
    if (!hud2) { hud_active = 0u; return; }
    hud_scene_valid = 1u; hud_status = 1u; hud_snapshot(1u);
}
void game_hud_end(void) {
    if (!hud_active) return; /* Repeated EXIT cannot re-publish a stale clear. */
    hud_active = 0u;
    hud_release = 1u;
    hud_snapshot(0u); /* Latest value supersedes any unsent game snapshot. */
}
int game_hud_owns_panel(void) { return hud_active || hud_release; }
int game_hud_releasing(void) { return hud_release != 0u; }
void game_hud_service(void) {
    if (hud_active) {
        u64 now = clint_getTime(BSP_CLINT);
        if (now < hud_window_start || hud_frames < hud_base_frames) {
            hud_window_start = now; hud_base_frames = hud_frames; hud_fps = 0u;
            if (hud_scene_valid) hud_snapshot(1u);
        } else {
            u64 elapsed = now - hud_window_start;
            if (elapsed >= (u64)BSP_CLINT_HZ) {
                u64 fps = ((u64)(hud_frames - hud_base_frames) * BSP_CLINT_HZ + elapsed / 2u) / elapsed;
                hud_fps = fps > 999u ? 999u : (uint32_t)fps;
                hud_window_start = now; hud_base_frames = hud_frames;
                if (hud_scene_valid) hud_snapshot(1u);
            }
        }
    }
    /* One nonblocking attempt. A busy mailbox leaves the latest bundle intact. */
    if (hud_dirty) {
        if (gpu_panel_publish(&hud_latest) == GPU_OK) hud_dirty = 0u;
    } else if (hud_release && !(gpu_get_panel_status() & GPU_PANEL_STATUS_BUSY)) {
        hud_release = 0u; /* Disable snapshot crossed VSYNC and was acknowledged. */
    }
}
