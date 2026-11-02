#ifndef APBTEST_ASSET_UPLOAD_H
#define APBTEST_ASSET_UPLOAD_H

#include <stdint.h>
#include "enemy_config.h"
/* -1: missing validated metadata; 0: transparent; 1: contains opaque pixels. */
int asset_upload_player_mask(void);

/* Fixed, byte-addressed resources.  The protocol never accepts a host
 * address; these values are also the contract used by the RTL controller. */
#define ASSET_ID_BG_ORIGINAL  1u
#define ASSET_ID_ENEMY_COLOR  2u
#define ASSET_ID_ENEMY_MASK   3u
#define ASSET_BG_BASE         0x02000000u
#define ASSET_BG_LENGTH       4147200u
#define ASSET_COLOR_BASE      0x02400000u
#define ASSET_COLOR_LENGTH    8192u
#define ASSET_MASK_BASE       0x02402000u
#define ASSET_MASK_LENGTH     512u
#define ASSET_HW_ABI          0x41530100u
#define ASSET_EXT_ABI         0x41530204u
#define ASSET_REG_EXT_ABI     0x154u
#define ASSET_REG_CAP_MASK    0x158u
#define ASSET_REG_CAP_MASK_HI 0x15Cu /* CMD: IDs 32..63 */

/* APB offsets are deliberately above the legacy 0x118 window. */
#define ASSET_REG_ABI         0x120u
#define ASSET_REG_ID          0x124u
#define ASSET_REG_OFFSET      0x128u
#define ASSET_REG_DATA        0x12Cu
#define ASSET_REG_CONTROL     0x130u
#define ASSET_REG_STATUS      0x134u
#define ASSET_REG_WORDS       0x138u
#define ASSET_REG_BEATS       0x13Cu
#define ASSET_REG_COMMITTED   0x140u
#define ASSET_REG_DONE_OFFSET 0x144u
#define ASSET_REG_ERROR       0x148u
#define ASSET_REG_BASE        0x14Cu
#define ASSET_REG_LENGTH      0x150u

#define ASSET_CTRL_CLAIM      1u
#define ASSET_CTRL_OPEN       2u
#define ASSET_CTRL_COMMIT     4u
#define ASSET_CTRL_ABORT      8u
#define ASSET_CTRL_RELEASE    16u
#define ASSET_CTRL_CLEAR      32u

#define ASSET_STATUS_OWNER       (1u << 0)
#define ASSET_STATUS_DATA_READY  (1u << 1)
#define ASSET_STATUS_COMMIT_READY (1u << 2)
#define ASSET_STATUS_INFLIGHT    (1u << 3)
#define ASSET_STATUS_DONE        (1u << 4)
#define ASSET_STATUS_ERROR       (1u << 5)
#define ASSET_STATUS_DRAIN       (1u << 6)
#define ASSET_STATUS_IDLE        (1u << 7)

void asset_upload_init(void);
void asset_upload_set_hooks(int (*prepare)(void), void (*resume)(void));
void asset_upload_set_resource_guard(int blocked);
int asset_upload_handle_line(const char *line);
int asset_upload_active(void);
int asset_upload_binary_active(void);
int asset_upload_route_byte(uint8_t byte);
int asset_upload_consume_byte(uint8_t byte);
int asset_upload_resource_guard(void);
void asset_upload_service(void);
uint32_t asset_upload_valid_mask(void);
uint32_t asset_upload_valid_mask_hi(void); /* IDs 32..63 */
uint32_t asset_upload_error(void); /* last error code (0 none); 15 DDR read-back CRC mismatch, 16 reader busy timeout */
/* Two logical 16x32 MSB-first masks; valid only after successful final drain.
 * A CPU reset deliberately requires re-upload even if DDR persists. */
const uint8_t *asset_upload_bullet_mask(uint32_t type);
/* BU3.1: four 8x12 enemy masks in 2x2 tiles of material0 (row stride2); PB remains material1. */
const uint8_t *asset_upload_missile_mask(uint32_t type);
const uint8_t *asset_upload_enemy_bullet_mask(uint32_t type);

#endif
