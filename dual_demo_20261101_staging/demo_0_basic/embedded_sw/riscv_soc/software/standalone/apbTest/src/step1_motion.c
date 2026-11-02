#include "step1_motion.h"

int32_t step1_next_axis(int32_t old, int32_t velocity,
                        int32_t minimum, int32_t maximum,
                        int32_t *next_velocity)
{
    int32_t next = old + velocity;
    if (next < minimum) {
        next = minimum;
        *next_velocity = -velocity;
    } else if (next > maximum) {
        next = maximum;
        *next_velocity = -velocity;
    } else {
        *next_velocity = velocity;
    }
    return next;
}

/* Current Step 1 uses 64x64 boxes, resolved one axis at a time, with maximum
 * per-step displacements of 8 pixels in X and 4 pixels in Y. Resolve X
 * before moving Y: a collision rolls back ONLY the colliding axis, so a
 * valid X coordinate remains an 8-pixel multiple. No floating point,
 * division, added GPU commands or shared CPU/GPU scene storage is needed. */
static int step1_boxes_overlap(const blit_object_t *a,
                               const blit_object_t *b)
{
    return a->enable && b->enable &&
           a->x < b->x + (int32_t)b->width &&
           a->x + (int32_t)a->width > b->x &&
           a->y < b->y + (int32_t)b->height &&
           a->y + (int32_t)a->height > b->y;
}

void step1_resolve_axis(const blit_object_t *shown,
                        blit_object_t *planned, u32 count, int axis_x)
{
    uint8_t held[STEP1_MAX_OBJECTS] = {0u};
    uint8_t responded[STEP1_MAX_OBJECTS] = {0u};
    u32 pass, i, j;

    /* Rolling one box back can obstruct another. The hold set only grows;
     * at most count boxes can join it. Start positions are non-overlapping:
     * ON/count changes reseed, X starts at shown, Y starts after resolved X.
     * A crowded collision may hold a box for this one simulation step. */
    for (pass = 0u; pass <= count; pass++) {
        int changed = 0;
        for (i = 0u; i < count; i++) {
            for (j = i + 1u; j < count; j++) {
                int32_t old_i, old_j, vi, vj;
                int approaching;
                if (!step1_boxes_overlap(&planned[i], &planned[j]))
                    continue;
                old_i = axis_x ? shown[i].x : shown[i].y;
                old_j = axis_x ? shown[j].x : shown[j].y;
                vi = axis_x ? planned[i].vx : planned[i].vy;
                vj = axis_x ? planned[j].vx : planned[j].vy;
                approaching = (old_i < old_j && vi > vj) ||
                              (old_i > old_j && vi < vj);

                /* Equal-size/equal-mass demo boxes exchange the normal
                 * component only when closing. Never invert a box twice
                 * on the same axis during a multi-box contact step. */
                if (approaching && !responded[i] && !responded[j]) {
                    if (axis_x) {
                        planned[i].vx = vj;
                        planned[j].vx = vi;
                    } else {
                        planned[i].vy = vj;
                        planned[j].vy = vi;
                    }
                    responded[i] = 1u;
                    responded[j] = 1u;
                }
                if (!held[i] || !held[j])
                    changed = 1;
                held[i] = 1u;
                held[j] = 1u;
                if (axis_x) {
                    planned[i].x = shown[i].x;
                    planned[j].x = shown[j].x;
                } else {
                    planned[i].y = shown[i].y;
                    planned[j].y = shown[j].y;
                }
            }
        }
        if (!changed)
            return;
    }

    /* Bounded conservative fallback: restore just this axis for every box.
     * No busy loop, re-seed or geometry change is allowed during movement. */
    for (i = 0u; i < count; i++) {
        if (axis_x)
            planned[i].x = shown[i].x;
        else
            planned[i].y = shown[i].y;
    }
}
