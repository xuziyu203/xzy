#ifndef SRC_STEP1_MOTION_H_
#define SRC_STEP1_MOTION_H_

#include <stdint.h>
#include "type.h"

#define STEP1_MAX_OBJECTS 20u

typedef struct {
    int32_t x;
    int32_t y;
    int32_t vx;
    int32_t vy;
    uint32_t width;
    uint32_t height;
    uint16_t color;
    uint8_t enable;
} blit_object_t;

int32_t step1_next_axis(int32_t old, int32_t velocity,
                        int32_t minimum, int32_t maximum,
                        int32_t *next_velocity);
void step1_resolve_axis(const blit_object_t *shown,
                        blit_object_t *planned, u32 count, int axis_x);

#endif /* SRC_STEP1_MOTION_H_ */
