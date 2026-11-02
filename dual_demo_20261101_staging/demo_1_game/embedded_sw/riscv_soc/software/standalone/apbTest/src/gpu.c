#include "gpu.h"
#include "enemy_config.h"
#include "io.h"

gpu_result_t gpu_background_get(u32 *config,u32 *frame,u32 *error)
{
    if (!config || !frame || !error) return GPU_ERR_PARAM;
    if (read_u32(GPU_REG_BACKGROUND_ABI) != GPU_BACKGROUND_ABI) return GPU_ERR_UNSUPPORTED;
    *config=read_u32(GPU_REG_BACKGROUND_CONFIG);
    *frame=read_u32(GPU_REG_BACKGROUND_FRAME);
    *error=read_u32(GPU_REG_BACKGROUND_ERROR);
    return GPU_OK;
}

gpu_result_t gpu_background_configure(u32 mode)
{
    u32 config,frame,error,status;
    gpu_result_t result;
    if (mode>GPU_BACKGROUND_I4) return GPU_ERR_PARAM;
    if (mode==GPU_BACKGROUND_I4 && read_u32(GPU_REG_BT_ABI)!=GPU_BT_ABI) return GPU_ERR_UNSUPPORTED;
    result=gpu_background_get(&config,&frame,&error);
    if (result!=GPU_OK) return result;
    status=read_u32(IO_APB_SLAVE_0_INPUT+0x168u);
    if ((status&0x81u)!=0x80u) return GPU_ERR_BUSY;
    write_u32(mode,GPU_REG_BACKGROUND_CONFIG);
    result=gpu_background_get(&config,&frame,&error);
    if (result!=GPU_OK) return result;
    status=read_u32(IO_APB_SLAVE_0_INPUT+0x168u);
    if (error==2u || (status&0x81u)!=0x80u) return GPU_ERR_BUSY;
    return !error && config==mode ? GPU_OK : GPU_ERR_HW;
}

/* CMD: I4 source (0 = game ID28/31, 1 = start menu ID32/33). Idle only, like the mode write. An older bit has no 0x324: only 0. */
gpu_result_t gpu_background_source(u32 menu)
{
    u32 status;
    if (menu>1u) return GPU_ERR_PARAM;
    if (read_u32(GPU_REG_DECOR_CAP)!=16u) return menu ? GPU_ERR_UNSUPPORTED : GPU_OK;
    status=read_u32(IO_APB_SLAVE_0_INPUT+0x168u);
    if ((status&0x81u)!=0x80u) return GPU_ERR_BUSY;
    write_u32(menu,GPU_REG_BG_SOURCE);
    return read_u32(GPU_REG_BG_SOURCE)==menu && read_u32(GPU_REG_BACKGROUND_ERROR)==0u ? GPU_OK : GPU_ERR_HW;
}

gpu_result_t gpu_enemy_key_get(u32 *config, u32 *frame)
{
    if (!config || !frame) return GPU_ERR_PARAM;
    if (read_u32(GPU_REG_ENEMY_KEY_ABI) != GPU_ENEMY_KEY_ABI)
        return GPU_ERR_UNSUPPORTED;
    *config = read_u32(GPU_REG_ENEMY_KEY_CONFIG);
    *frame = read_u32(GPU_REG_ENEMY_KEY_FRAME);
    return GPU_OK;
}

gpu_result_t gpu_enemy_key_configure(u32 config)
{
    u32 status, actual;
    if (config & ~GPU_ENEMY_KEY_CONFIG_MASK) return GPU_ERR_PARAM;
    if (read_u32(GPU_REG_ENEMY_KEY_ABI) != GPU_ENEMY_KEY_ABI)
        return GPU_ERR_UNSUPPORTED;
    status = read_u32(IO_APB_SLAVE_0_INPUT + 0x168u);
    if ((status & 0x81u) != 0x80u) return GPU_ERR_BUSY;
    write_u32(config, GPU_REG_ENEMY_KEY_CONFIG);
    actual = read_u32(GPU_REG_ENEMY_KEY_CONFIG);
    status = read_u32(IO_APB_SLAVE_0_INPUT + 0x168u);
    if ((status & 0x81u) != 0x80u) return GPU_ERR_BUSY;
    return actual == config ? GPU_OK : GPU_ERR_HW;
}

u32 gpu_get_status(void)
{
    return read_u32(GPU_REG_STATUS);
}

u32 gpu_get_fifo_status(void)
{
    return read_u32(GPU_REG_FIFO_STATUS);
}

u32 gpu_get_error_info(void)
{
    return read_u32(GPU_REG_ERROR_INFO);
}

u32 gpu_get_display_status(void)
{
    return read_u32(GPU_REG_DISPLAY_STATUS);
}

u32 gpu_get_display_generation(void)
{
    return read_u32(GPU_REG_DISPLAY_GENERATION);
}

u32 gpu_get_async_control(void) { return read_u32(GPU_REG_ASYNC_CONTROL); }
u32 gpu_get_async_q0_seq(void) { return read_u32(GPU_REG_ASYNC_Q0_SEQ); }
u32 gpu_get_async_gpu_seq(void) { return read_u32(GPU_REG_ASYNC_GPU_SEQ); }
u32 gpu_get_async_render_cycles(void) { return read_u32(GPU_REG_ASYNC_RENDER_CYCLES); }
u32 gpu_get_async_owner_wait(void) { return read_u32(GPU_REG_ASYNC_OWNER_WAIT); }
u32 gpu_get_async_event_status(void) { return read_u32(GPU_REG_ASYNC_EVENT_STATUS); }
u32 gpu_get_panel_status(void) { return read_u32(GPU_REG_PANEL_STATUS); }

gpu_result_t gpu_panel_publish(const gpu_panel_snapshot_t *snapshot)
{
    if (snapshot == 0)
        return GPU_ERR_PARAM;
    /* A busy mailbox owns the published snapshot until the pixel-domain
     * acknowledge returns.  Drop this window instead of overwriting it. */
    if ((gpu_get_panel_status() & GPU_PANEL_STATUS_BUSY) != 0u)
        return GPU_ERR_BUSY;
    write_u32(snapshot->cpu_row, GPU_REG_PANEL_CPU_ROW);
    write_u32(snapshot->blit_row, GPU_REG_PANEL_BLIT_ROW);
    write_u32(snapshot->rect_row, GPU_REG_PANEL_RECT_ROW);
    write_u32(snapshot->blit_time, GPU_REG_PANEL_BLIT_TIME);
    write_u32(snapshot->rect_time, GPU_REG_PANEL_RECT_TIME);
    write_u32(snapshot->flags, GPU_REG_PANEL_FLAGS);
    write_u32(snapshot->error, GPU_REG_PANEL_ERROR);
    write_u32(1u, GPU_REG_PANEL_COMMIT);
    return GPU_OK;
}

gpu_result_t gpu_set_async_enable(int enable)
{
    if ((enable != 0) && (enable != 1))
        return GPU_ERR_PARAM;
    write_u32(GPU_ASYNC_ABI_MAGIC | (enable ? 1u : 0u),
              GPU_REG_ASYNC_CONTROL);
    if ((enable != 0) && ((gpu_get_async_control() & 1u) == 0u))
        return GPU_ERR_BUSY;
    return GPU_OK;
}

gpu_result_t gpu_get_perf(gpu_perf_t *perf)
{
    gpu_perf_t sample;
    u32 status;

    if (perf == 0) {
        return GPU_ERR_PARAM;
    }

    status = gpu_get_status();
    if ((status & GPU_STATUS_ERROR) == 0u &&
        ((status & GPU_STATUS_BUSY) != 0u ||
         (status & GPU_STATUS_DONE) == 0u)) {
        return GPU_ERR_BUSY;
    }

    sample.blockcopy_cycles = read_u32(GPU_REG_BLOCKCOPY_CYCLES);
    sample.blockcopy_total_cycles = read_u32(GPU_REG_BLOCKCOPY_TOTAL_CYCLES);
    sample.rect_last_cycles = read_u32(GPU_REG_RECT_LAST_CYCLES);
    sample.rect_total_cycles = read_u32(GPU_REG_RECT_TOTAL_CYCLES);
    sample.frame_cycles = read_u32(GPU_REG_FRAME_CYCLES);
    sample.rect_count = read_u32(GPU_REG_RECT_COUNT);

    // Recheck readiness; the caller must serialize this read with submit.
    status = gpu_get_status();
    if ((status & GPU_STATUS_ERROR) != 0u) {
        *perf = sample;
        return GPU_ERR_HW;
    }
    if ((status & (GPU_STATUS_BUSY | GPU_STATUS_DONE)) != GPU_STATUS_DONE) {
        return GPU_ERR_BUSY;
    }

    *perf = sample;
    return GPU_OK;
}

gpu_result_t gpu_begin_frame(void)
{
    u32 fifo_status;

    u32 status;

    status = gpu_get_status();
    if ((status & GPU_STATUS_ERROR) != 0u) {
        return GPU_ERR_HW;
    }

    if ((status & GPU_STATUS_BUSY) != 0u) {
        return GPU_ERR_BUSY;
    }

    fifo_status = gpu_get_fifo_status();
    if ((fifo_status & GPU_FIFO_STATUS_EMPTY) == 0u) {
        return GPU_ERR_FIFO_NOT_EMPTY;
    }

    return GPU_OK;
}

gpu_result_t gpu_set_target(u32 target_id)
{
    if (target_id > GPU_TARGET_Q3)
        return GPU_ERR_PARAM;
    write_u32(target_id, GPU_REG_TARGET_CONTEXT);
    return GPU_OK;
}

gpu_result_t gpu_set_display(u32 mode, u32 full_source)
{
    if ((mode > GPU_DISPLAY_MODE_QUAD) ||
        (full_source > GPU_FULL_SOURCE_CPU))
        return GPU_ERR_PARAM;
    write_u32((full_source << 2) | mode, GPU_REG_DISPLAY_CONTROL);
    return GPU_OK;
}

gpu_result_t gpu_push_rect(
    u32 center_x,
    u32 center_y,
    u32 width,
    u32 height,
    u16 color
)
{
    u32 cmd_word0;
    u32 cmd_word1;
    u32 cmd_word2;
    u32 cmd_word3;
    u32 status;
    u32 fifo_status;

    if ((center_x >= GPU_SCREEN_WIDTH) ||
        (center_y >= GPU_SCREEN_HEIGHT) ||
        (width == 0u) ||
        (width > GPU_SCREEN_WIDTH) ||
        (height == 0u) ||
        (height > GPU_SCREEN_HEIGHT)) {
        return GPU_ERR_PARAM;
    }

    status = gpu_get_status();
    if ((status & GPU_STATUS_ERROR) != 0u) {
        return GPU_ERR_HW;
    }

    if ((status & GPU_STATUS_BUSY) != 0u) {
        return GPU_ERR_BUSY;
    }

    fifo_status = gpu_get_fifo_status();
    if ((fifo_status & GPU_FIFO_STATUS_FULL) != 0u) {
        return GPU_ERR_FIFO_FULL;
    }

    cmd_word0 = GPU_CMD_OP_RECT |
                ((center_x & GPU_CMD_FIELD_12BIT_MASK) <<
                    GPU_CMD_WORD0_X_SHIFT) |
                ((center_y & GPU_CMD_FIELD_12BIT_MASK) <<
                    GPU_CMD_WORD0_Y_SHIFT);
    cmd_word1 = (width & GPU_CMD_FIELD_12BIT_MASK) |
                ((height & GPU_CMD_FIELD_12BIT_MASK) <<
                    GPU_CMD_WORD1_HEIGHT_SHIFT);
    cmd_word2 = 0u;
    cmd_word3 = ((u32)color & 0x0000FFFFu) <<
                GPU_CMD_WORD3_COLOR_SHIFT;

    write_u32(cmd_word0, GPU_REG_CMD_WORD0);
    write_u32(cmd_word1, GPU_REG_CMD_WORD1);
    write_u32(cmd_word2, GPU_REG_CMD_WORD2);
    write_u32(cmd_word3, GPU_REG_CMD_WORD3);
    write_u32(GPU_CMD_PUSH, GPU_REG_CMD_PUSH);

    fifo_status = gpu_get_fifo_status();
    if ((fifo_status & GPU_FIFO_STATUS_PUSH_REJECT) != 0u) {
        return GPU_ERR_PUSH_REJECT;
    }

    return GPU_OK;
}

gpu_result_t gpu_push_blit(
    u32 src_offset,
    u32 dst_x,
    u32 dst_y,
    u32 width,
    u32 height,
    u32 src_stride
)
{
    /* Compatibility entry: validate FULL geometry without changing the
     * target context selected by existing callers. */
    return gpu_push_blit_target(GPU_TARGET_FULL_BLIT, src_offset, dst_x,
                               dst_y, width, height, src_stride);
}

gpu_result_t gpu_push_blit_target(
    u32 target_id, u32 src_offset, u32 dst_x, u32 dst_y,
    u32 width, u32 height, u32 src_stride
)
{
    u32 status;
    u32 fifo_status;
    u32 src_x;
    u32 src_y;
    u32 cmd_word0;
    u32 cmd_word1;
    u32 surface_width = target_id >= GPU_TARGET_Q0 ? 960u : GPU_SCREEN_WIDTH;
    u32 surface_height = target_id >= GPU_TARGET_Q0 ? 540u : GPU_SCREEN_HEIGHT;
    u32 stride = surface_width * 2u;

    // Check ranges before subtraction/addition, including hostile u32 inputs.
    if ((target_id > GPU_TARGET_Q3) ||
        (width == 0u) || (width > surface_width) ||
        (height == 0u) || (height > surface_height) ||
        ((width & 7u) != 0u) || ((dst_x & 7u) != 0u) ||
        ((src_offset & 15u) != 0u) ||
        (src_stride != stride) ||
        (src_offset >= stride * surface_height) ||
        (dst_x > surface_width - width) ||
        (dst_y > surface_height - height)) {
        return GPU_ERR_PARAM;
    }

    src_x = (src_offset % stride) / 2u;
    src_y = src_offset / stride;
    if ((src_x > surface_width - width) ||
        (src_y > surface_height - height)) {
        return GPU_ERR_PARAM;
    }

    status = gpu_get_status();
    if ((status & GPU_STATUS_ERROR) != 0u) {
        return GPU_ERR_HW;
    }
    if ((status & GPU_STATUS_BUSY) != 0u) {
        return GPU_ERR_BUSY;
    }
    fifo_status = gpu_get_fifo_status();
    if ((fifo_status & GPU_FIFO_STATUS_FULL) != 0u) {
        return GPU_ERR_FIFO_FULL;
    }

    // Preserve the existing v2 field layout; RECT keeps center coordinates.
    cmd_word0 = GPU_CMD_OP_BLIT |
                (dst_x << GPU_CMD_WORD0_X_SHIFT) |
                (dst_y << GPU_CMD_WORD0_Y_SHIFT);
    cmd_word1 = width | (height << GPU_CMD_WORD1_HEIGHT_SHIFT);
    write_u32(cmd_word0, GPU_REG_CMD_WORD0);
    write_u32(cmd_word1, GPU_REG_CMD_WORD1);
    write_u32(src_offset, GPU_REG_CMD_WORD2);
    write_u32(src_stride, GPU_REG_CMD_WORD3);
    write_u32(GPU_CMD_PUSH, GPU_REG_CMD_PUSH);

    fifo_status = gpu_get_fifo_status();
    if ((fifo_status & GPU_FIFO_STATUS_PUSH_REJECT) != 0u) {
        return GPU_ERR_PUSH_REJECT;
    }
    return GPU_OK;
}

gpu_result_t gpu_submit_frame(void)
{
    u32 fifo_status;
    u32 status;

    status = gpu_get_status();
    if ((status & GPU_STATUS_ERROR) != 0u) {
        return GPU_ERR_HW;
    }

    if ((status & GPU_STATUS_BUSY) != 0u) {
        return GPU_ERR_BUSY;
    }

    fifo_status = gpu_get_fifo_status();
    if ((fifo_status & GPU_FIFO_STATUS_EMPTY) != 0u) {
        return GPU_ERR_EMPTY_FRAME;
    }

    write_u32(GPU_CTRL_FRAME_START, GPU_REG_CTRL);

    return GPU_OK;
}

gpu_result_t gpu_wait_done(u32 poll_limit)
{
    u32 poll_count;
    u32 status;
    u32 error_info;

    for (poll_count = 0u;
         poll_count < poll_limit;
         poll_count++) {
        status = gpu_get_status();

        if ((status & GPU_STATUS_ERROR) != 0u) {
            error_info = gpu_get_error_info();
            (void)error_info;
            return GPU_ERR_HW;
        }

        if ((status & GPU_STATUS_DONE) != 0u) {
            return GPU_OK;
        }
    }

    return GPU_ERR_TIMEOUT;
}

gpu_result_t gpu_draw_rect(
    u32 center_x,
    u32 center_y,
    u32 width,
    u32 height,
    u16 color
)
{
    gpu_result_t result;

    result = gpu_begin_frame();
    if (result != GPU_OK) {
        return result;
    }

    result = gpu_push_rect(center_x, center_y, width, height, color);
    if (result != GPU_OK) {
        return result;
    }

    result = gpu_submit_frame();
    if (result != GPU_OK) {
        return result;
    }

    return gpu_wait_done(GPU_DONE_POLL_LIMIT);
}

/* This table has no renderer/writer side effects. Claim remains the lifecycle's
 * existing 0x164 transaction. Driver calls are bounded and never spin. */
static u32 scene_read(u32 off){return read_u32(IO_APB_SLAVE_0_INPUT+off);}
static void scene_write(u32 off,u32 v){write_u32(v,IO_APB_SLAVE_0_INPUT+off);}

gpu_result_t gpu_player_get(u32 *config,u32 *frame){
 if(!config || !frame)return GPU_ERR_PARAM;
 *config=*frame=0;
 if(scene_read(0x280)!=GPU_PLAYER_ABI)return GPU_ERR_UNSUPPORTED;
 *config=scene_read(0x284);*frame=scene_read(0x288);return GPU_OK;
}
static gpu_result_t gpu_player_write(u32 enable,u32 x,u32 y,u32 table_mask);
gpu_result_t gpu_player_configure(u32 enable,u32 x,u32 y){return gpu_player_write(enable,x,y,3u);}
/* GHD3S PIPE publish: the N+1 tables may already be sealed (ready); none may be mid-staging. */
gpu_result_t gpu_player_configure_sealed(u32 enable,u32 x,u32 y){return gpu_player_write(enable,x,y,2u);}
static gpu_result_t gpu_player_write(u32 enable,u32 x,u32 y,u32 table_mask){
 u32 config,frame,w;gpu_result_t r;
 if(enable>1u || x>1920u-PLAYER_WIDTH || y>1080u-PLAYER_HEIGHT)return GPU_ERR_PARAM;
 r=gpu_player_get(&config,&frame);if(r!=GPU_OK)return enable?r:GPU_OK;
 if((scene_read(0x168)&0x81u)!=0x80u || (scene_read(0x228)&table_mask) || (scene_read(0x260)&table_mask) || (scene_read(0x2b0)&table_mask) ||
    (scene_read(0x2e8)&table_mask) || (scene_read(0x360)&table_mask))return GPU_ERR_BUSY;
 if(enable && scene_read(0x238)!=1u)return GPU_ERR_PARAM;
 w=(enable<<31)|(y<<11)|x;scene_write(0x284,w);
 if(scene_read(0x28c)==2u)return GPU_ERR_BUSY;
 return scene_read(0x284)==w && !scene_read(0x28c)?GPU_OK:GPU_ERR_HW;
}
gpu_result_t gpu_enemy_scene_capacity(u32 *cap){
 if(!cap)return GPU_ERR_PARAM;
 *cap=0;
 if(scene_read(0x200)!=GPU_ENEMY_SCENE_ABI)return GPU_ERR_UNSUPPORTED;
 *cap=scene_read(0x204);if(!*cap || *cap>65535u)return GPU_ERR_HW;return GPU_OK;
}
u32 gpu_enemy_scene_frame_sequence(void){return scene_read(0x22c);}
void gpu_enemy_scene_cancel(void){scene_write(0x208,4);}
gpu_result_t gpu_enemy_scene_mode(u32 multi){
 u32 cap;gpu_result_t r;if(multi>1u)return GPU_ERR_PARAM;
 r=gpu_enemy_scene_capacity(&cap);if(r!=GPU_OK)return multi?r:GPU_OK;
 if((scene_read(0x168)&0x81u)!=0x80u)return GPU_ERR_BUSY;
 if((scene_read(0x2b0)&3u) || (scene_read(0x2e8)&3u) || (scene_read(0x360)&3u) || (!multi && (scene_read(0x2c0) || scene_read(0x2f8) || scene_read(0x370))))return GPU_ERR_BUSY;
 scene_write(0x208,4);scene_write(0x238,multi);
 return scene_read(0x238)==multi && scene_read(0x234)==0?GPU_OK:GPU_ERR_HW;
}
gpu_result_t gpu_enemy_scene_open(u32 count,u32 sequence){
 u32 cap;gpu_result_t r=gpu_enemy_scene_capacity(&cap);if(r!=GPU_OK)return r;
 if(count>cap)return GPU_ERR_PARAM;
 if(scene_read(0x228)&3u)return GPU_ERR_BUSY;
 scene_write(0x208,1);scene_write(0x20c,count);scene_write(0x224,sequence);
 return scene_read(0x234)==0 && scene_read(0x20c)==count && scene_read(0x224)==sequence?GPU_OK:GPU_ERR_HW;
}
gpu_result_t gpu_enemy_scene_write(const u32 words[4]){
 u32 i;if(!words)return GPU_ERR_PARAM;
 if((scene_read(0x228)&7u)!=2u)return GPU_ERR_BUSY;
 for(i=0;i<4;i++)scene_write(0x214+4*i,words[i]);
 return scene_read(0x234)==0?GPU_OK:GPU_ERR_HW;
}
gpu_result_t gpu_enemy_scene_seal(u32 count,u32 sequence){
 if(scene_read(0x234)!=0)return GPU_ERR_HW;
 if(scene_read(0x210)!=count || scene_read(0x20c)!=count || scene_read(0x224)!=sequence)return GPU_ERR_PARAM;
 scene_write(0x208,2);
 return (scene_read(0x228)&7u)==1u && scene_read(0x234)==0?GPU_OK:GPU_ERR_HW;
}

gpu_result_t gpu_bullet_scene_capacity(u32 *cap){
 if(!cap)return GPU_ERR_PARAM;
 *cap=0;
 if(scene_read(0x240)!=GPU_BULLET_SCENE_ABI ||
    (scene_read(0x274)&GPU_BULLET_SCENE_FEATURES)!=GPU_BULLET_SCENE_FEATURES)return GPU_ERR_UNSUPPORTED;
 *cap=scene_read(0x244);return *cap==BULLET_CAPACITY?GPU_OK:GPU_ERR_HW;
}
void gpu_bullet_scene_cancel(void){scene_write(0x248,4);}
gpu_result_t gpu_bullet_scene_mode(u32 enable){
 u32 cap;gpu_result_t r;if(enable>1u)return GPU_ERR_PARAM;
 r=gpu_bullet_scene_capacity(&cap);if(r!=GPU_OK)return enable?r:GPU_OK;
 if((scene_read(0x168)&0x81u)!=0x80u)return GPU_ERR_BUSY;
 if((scene_read(0x228)&3u) || (scene_read(0x260)&3u) || (scene_read(0x2b0)&3u) || (scene_read(0x2e8)&3u) || (scene_read(0x360)&3u))return GPU_ERR_BUSY;
 scene_write(0x270,enable);return scene_read(0x270)==enable && !scene_read(0x26c)?GPU_OK:GPU_ERR_HW;
}
gpu_result_t gpu_bullet_scene_open(u32 count,u32 sequence){
 u32 cap;gpu_result_t r=gpu_bullet_scene_capacity(&cap);if(r!=GPU_OK)return r;
 if(count>cap)return GPU_ERR_PARAM;
 if(scene_read(0x260)&3u)return GPU_ERR_BUSY;
 scene_write(0x248,1);scene_write(0x24c,count);scene_write(0x25c,sequence);
 return !scene_read(0x26c) && scene_read(0x24c)==count && scene_read(0x25c)==sequence?GPU_OK:GPU_ERR_HW;
}
int gpu_bullet_scene_words_ok(const u32 words[2]){
 int x,y;if(!words)return 0;
 x=(int)(words[0]&65535u);if(x&32768)x-=65536;
 y=(int)(words[1]&65535u);if(y&32768)y-=65536;
 u32 layout=(words[0]>>19)&1u,mask=(words[0]>>16)&7u,style=(words[1]>>16)&3u;
 return !((words[0]&0xff000000u) || (!layout && ((words[0]>>20)&15u)) || (words[1]&0xf0000000u) || /* BU6: rank word1[27:18]; GHD5R: dir word0[23:20] */
    (layout?!((BULLET_TRIPLE_STYLE_MASK>>style)&1u):mask>3u) || /* GHD5: per-style triple layout and span */
    x < -(layout?(int)BULLET_TRI_ROT_WMAX-1:27) || x > 1919 || y < -(layout?(int)BULLET_TRI_ROT_HMAX-1:11) || y > 1079);
}
gpu_result_t gpu_bullet_scene_write(const u32 words[2]){
 if(!gpu_bullet_scene_words_ok(words))return GPU_ERR_PARAM;
 if((scene_read(0x260)&15u)!=2u)return GPU_ERR_BUSY;
 scene_write(0x254,words[0]);scene_write(0x258,words[1]);return scene_read(0x26c)?GPU_ERR_HW:GPU_OK;
}
/* The table error is sticky until open/cancel and, once set, rejects every
 * later data write, so error==0 and written==expected after the batch prove
 * every pair of it was accepted in order (STAGE1_DESIGN O3). */
gpu_result_t gpu_bullet_scene_batch_open(void){return (scene_read(0x260)&15u)==2u?GPU_OK:GPU_ERR_BUSY;}
void gpu_bullet_scene_put(const u32 words[2]){scene_write(0x254,words[0]);scene_write(0x258,words[1]);}
gpu_result_t gpu_bullet_scene_batch_close(u32 written){
 if(scene_read(0x26c))return GPU_ERR_HW;
 return scene_read(0x250)==written?GPU_OK:GPU_ERR_HW;
}
gpu_result_t gpu_bullet_scene_seal(u32 count,u32 sequence){
 if(scene_read(0x26c))return GPU_ERR_HW;
 if(scene_read(0x250)!=count || scene_read(0x24c)!=count || scene_read(0x25c)!=sequence)return GPU_ERR_PARAM;
 scene_write(0x248,2);return (scene_read(0x260)&15u)==1u && !scene_read(0x26c)?GPU_OK:GPU_ERR_HW;
}

gpu_result_t gpu_player_bullet_scene_capacity(u32 *cap){
 if(!cap)return GPU_ERR_PARAM;
 *cap=0;
 if(scene_read(0x290)!=GPU_PLAYER_BULLET_SCENE_ABI ||
    (scene_read(0x2c4)&GPU_PLAYER_BULLET_SCENE_FEATURES)!=GPU_PLAYER_BULLET_SCENE_FEATURES)return GPU_ERR_UNSUPPORTED;
 *cap=scene_read(0x294);return *cap==16u?GPU_OK:GPU_ERR_UNSUPPORTED;
}
void gpu_player_bullet_scene_cancel(void){scene_write(0x298,4);}
gpu_result_t gpu_player_bullet_scene_mode(u32 enable){
 u32 cap;gpu_result_t r;if(enable>1u)return GPU_ERR_PARAM;
 r=gpu_player_bullet_scene_capacity(&cap);if(r!=GPU_OK)return enable?r:GPU_OK;
 if((scene_read(0x168)&0x81u)!=0x80u || (scene_read(0x228)&3u) ||
    (scene_read(0x260)&3u) || (scene_read(0x2b0)&3u) || (scene_read(0x2e8)&3u) || (scene_read(0x360)&3u))return GPU_ERR_BUSY;
 if(enable && scene_read(0x238)!=1u)return GPU_ERR_PARAM;
 scene_write(0x2c0,enable);return scene_read(0x2c0)==enable && !scene_read(0x2bc)?GPU_OK:GPU_ERR_HW;
}
gpu_result_t gpu_player_bullet_scene_open(u32 count,u32 sequence){
 u32 cap;gpu_result_t r=gpu_player_bullet_scene_capacity(&cap);if(r!=GPU_OK)return r;
 if(count>cap)return GPU_ERR_PARAM;
 if(scene_read(0x2b0)&3u)return GPU_ERR_BUSY;
 scene_write(0x298,1);scene_write(0x29c,count);scene_write(0x2ac,sequence);
 return !scene_read(0x2bc) && scene_read(0x29c)==count && scene_read(0x2ac)==sequence?GPU_OK:GPU_ERR_HW;
}
gpu_result_t gpu_player_bullet_scene_write(const u32 words[2]){
 int x,y;if(!words)return GPU_ERR_PARAM;
 x=(int)(words[0]&65535u);if(x&32768)x-=65536;
 y=(int)(words[1]&65535u);if(y&32768)y-=65536;
 if((words[0]&0xffff0000u) || (words[1]&0xffe00000u) || !(words[1]&0x10000u) ||
    x < -15 || x > 1919 || y < -31 || y > 1079)return GPU_ERR_PARAM;
 if((scene_read(0x2b0)&15u)!=2u)return GPU_ERR_BUSY;
 scene_write(0x2a4,words[0]);scene_write(0x2a8,words[1]);return scene_read(0x2bc)?GPU_ERR_HW:GPU_OK;
}
gpu_result_t gpu_player_bullet_scene_seal(u32 count,u32 sequence){
 if(scene_read(0x2bc))return GPU_ERR_HW;
 if(scene_read(0x2a0)!=count || scene_read(0x29c)!=count || scene_read(0x2ac)!=sequence)return GPU_ERR_PARAM;
 scene_write(0x298,2);return (scene_read(0x2b0)&15u)==1u && !scene_read(0x2bc)?GPU_OK:GPU_ERR_HW;
}

gpu_result_t gpu_effect_scene_capacity(u32 *cap){
 if(!cap)return GPU_ERR_PARAM;
 *cap=0;
 if(scene_read(0x2c8)!=GPU_EFFECT_SCENE_ABI ||
    (scene_read(0x2fc)&GPU_EFFECT_SCENE_FEATURES)!=GPU_EFFECT_SCENE_FEATURES)return GPU_ERR_UNSUPPORTED;
 *cap=scene_read(0x2cc);return *cap==EFFECT_CAPACITY?GPU_OK:GPU_ERR_UNSUPPORTED;
}
void gpu_effect_scene_cancel(void){scene_write(0x2d0,4);}
gpu_result_t gpu_effect_scene_mode(u32 enable){
 u32 cap;gpu_result_t r;if(enable>1u)return GPU_ERR_PARAM;
 r=gpu_effect_scene_capacity(&cap);if(r!=GPU_OK)return enable?r:GPU_OK;
 if((scene_read(0x168)&0x81u)!=0x80u || (scene_read(0x228)&3u) ||
    (scene_read(0x260)&3u) || (scene_read(0x2b0)&3u) || (scene_read(0x2e8)&3u) || (scene_read(0x360)&3u))return GPU_ERR_BUSY;
 if(enable && scene_read(0x238)!=1u)return GPU_ERR_PARAM;
 scene_write(0x2f8,enable);return scene_read(0x2f8)==enable && !scene_read(0x2f4)?GPU_OK:GPU_ERR_HW;
}
gpu_result_t gpu_effect_scene_open(u32 count,u32 sequence){
 u32 cap;gpu_result_t r=gpu_effect_scene_capacity(&cap);if(r!=GPU_OK)return r;
 if(count>cap)return GPU_ERR_PARAM;
 if(scene_read(0x2e8)&3u)return GPU_ERR_BUSY;
 scene_write(0x2d0,1);scene_write(0x2d4,count);scene_write(0x2e4,sequence);
 return !scene_read(0x2f4) && scene_read(0x2d4)==count && scene_read(0x2e4)==sequence?GPU_OK:GPU_ERR_HW;
}
gpu_result_t gpu_effect_scene_write(const u32 words[2]){
 static const uint8_t frames[EFFECT_TYPE_COUNT]=EFFECT_FRAMES_VALUES;
 u32 x,y,size,type;if(!words)return GPU_ERR_PARAM;
 x=words[0]&0xfffu;y=words[1]&0xfffu;type=(words[0]>>15)&3u;size=(type&1u)?128u:64u;
 if((words[0]&0xfffe0000u) || (words[1]&0xfffff000u) || ((words[0]>>12)&7u)>=frames[type])return GPU_ERR_PARAM;
 if(type==3u){
  int32_t sx=(int32_t)(x^0x800u)-0x800,sy=(int32_t)(y^0x800u)-0x800;
  if(sx>=1920 || sx+128<=0 || sy>=1080 || sy+128<=0)return GPU_ERR_PARAM;
 }else if((x&7u) || x+size>1920u || y+size>1080u)return GPU_ERR_PARAM;
 if((scene_read(0x2e8)&15u)!=2u)return GPU_ERR_BUSY;
 scene_write(0x2dc,words[0]);scene_write(0x2e0,words[1]);return scene_read(0x2f4)?GPU_ERR_HW:GPU_OK;
}
gpu_result_t gpu_effect_scene_seal(u32 count,u32 sequence){
 if(scene_read(0x2f4))return GPU_ERR_HW;
 if(scene_read(0x2d8)!=count || scene_read(0x2d4)!=count || scene_read(0x2e4)!=sequence)return GPU_ERR_PARAM;
 scene_write(0x2d0,2);return (scene_read(0x2e8)&15u)==1u && !scene_read(0x2f4)?GPU_OK:GPU_ERR_HW;
}

gpu_result_t gpu_missile_scene_capacity(u32 *cap){
 if(!cap)return GPU_ERR_PARAM;
 *cap=0;
 if(scene_read(0x340)!=GPU_MISSILE_SCENE_ABI ||
    (scene_read(0x374)&GPU_MISSILE_SCENE_FEATURES)!=GPU_MISSILE_SCENE_FEATURES)return GPU_ERR_UNSUPPORTED;
 *cap=scene_read(0x344);return *cap==MISSILE_CAPACITY?GPU_OK:GPU_ERR_UNSUPPORTED;
}
void gpu_missile_scene_cancel(void){scene_write(0x348,4);}
gpu_result_t gpu_missile_scene_mode(u32 enable){
 u32 cap;gpu_result_t r;if(enable>1u)return GPU_ERR_PARAM;
 r=gpu_missile_scene_capacity(&cap);if(r!=GPU_OK)return enable?r:GPU_OK;
 if((scene_read(0x168)&0x81u)!=0x80u || (scene_read(0x228)&3u) ||
    (scene_read(0x260)&3u) || (scene_read(0x2b0)&3u) || (scene_read(0x2e8)&3u) || (scene_read(0x360)&3u))return GPU_ERR_BUSY;
 if(enable && scene_read(0x238)!=1u)return GPU_ERR_PARAM;
 scene_write(0x370,enable);return scene_read(0x370)==enable && !scene_read(0x36c)?GPU_OK:GPU_ERR_HW;
}
gpu_result_t gpu_missile_scene_open(u32 count,u32 sequence){
 u32 cap;gpu_result_t r=gpu_missile_scene_capacity(&cap);if(r!=GPU_OK)return r;
 if(count>cap)return GPU_ERR_PARAM;
 if(scene_read(0x360)&3u)return GPU_ERR_BUSY;
 scene_write(0x348,1);scene_write(0x34c,count);scene_write(0x35c,sequence);
 return !scene_read(0x36c) && scene_read(0x34c)==count && scene_read(0x35c)==sequence?GPU_OK:GPU_ERR_HW;
}
gpu_result_t gpu_missile_scene_write(const u32 words[2]){
 int x,y;if(!words)return GPU_ERR_PARAM;
 x=(int)(words[0]&65535u);if(x&32768)x-=65536;
 y=(int)(words[1]&65535u);if(y&32768)y-=65536;
 if((words[0]&0xfffc0000u) || !(words[0]&0x20000u) || (words[1]&0xfff80000u) ||
    x < -((words[0]&0x10000u)?11:7) || x > 1919 || y < -31 || y > 1079)return GPU_ERR_PARAM;
 if((scene_read(0x360)&15u)!=2u)return GPU_ERR_BUSY;
 scene_write(0x354,words[0]);scene_write(0x358,words[1]);return scene_read(0x36c)?GPU_ERR_HW:GPU_OK;
}
gpu_result_t gpu_missile_scene_seal(u32 count,u32 sequence){
 if(scene_read(0x36c))return GPU_ERR_HW;
 if(scene_read(0x350)!=count || scene_read(0x34c)!=count || scene_read(0x35c)!=sequence)return GPU_ERR_PARAM;
 scene_write(0x348,2);return (scene_read(0x360)&15u)==1u && !scene_read(0x36c)?GPU_OK:GPU_ERR_HW;
}

