#ifndef BUILD_VARIANT_GAME /* GHD3: full UART build only (single ELF: main_single.c) */
#include "gpu.h"
#include "renderer.h"
#include "cpu_writer.h"
#include "bg_uart.h"
#include "bg_ddr_adapter.h"
#include "asset_upload.h"
#include "enemy_test.h"
#include "uart_reply.h"
#include "game_hud.h"
#include "game_lifecycle.h"
#include "demo_command_text.h"
#include "bsp.h"
#include <stdint.h>
#include <string.h>
#define DEMO_UART_LINE_SIZE 96u
#define DEMO_UART_POLL_BUDGET 64u
static char demo_uart_line[DEMO_UART_LINE_SIZE];
static unsigned demo_uart_line_length,demo_uart_line_overflow;
/* Stage-1 D: CLINT time at which the pending line was completely received. */
static u64 demo_uart_line_done_at;
static int demo_uart_protocol_active(void){return asset_upload_active() || bg_uart_preparing() || bg_uart_binary_active() || bg_uart_recovery_active();}
static void game_text_reply(const char *s){if(demo_uart_protocol_active())uart_writeStr(BSP_UART_TERMINAL,s);else uart_reply_event(s);}
static void demo_uart_status(void){
 if(demo_uart_protocol_active()){game_lifecycle_status();return;}
 if(!uart_reply_begin(UART_REPLY_QUERY_MAX))return;
 game_lifecycle_status();uart_reply_commit();
}
static int demo_uart_line_ready(const char *line)

{

    line = demo_skip_spaces(line);

    /* Silent complete-state input never reserves response space. Binary

     * payload has already been routed by the sole physical UART reader. */

    if (enemy_test_input_line(line)) return 1;

    if (demo_uart_protocol_active()) return !uart_reply_pending();

    if (strncmp(line, "ENEMY ", 6) == 0 || strncmp(line,"PLAYER ",7)==0 || demo_text_equal(line, "STATUS"))

        return uart_reply_query_ready();

    /* A protocol BEGIN / synchronous response must follow the complete text

     * already queued, even if ENEMY has finished restoring its resources. */

    return !uart_reply_pending();

}



static void demo_uart_apply_line(const char *line){
 const char *argument;int peer;
 if(enemy_test_input_line(line)){
  (void)enemy_test_handle_line(line,demo_uart_protocol_active() || !enemy_test_active() || game_lifecycle_busy());return;
 }
 line=demo_skip_spaces(line);
 /* Removed commands are rejected in every mode, with no hardware writes. */
 if(demo_command_arg(line,"MODE",&argument) || demo_command_arg(line,"FULL",&argument) ||
    demo_command_arg(line,"RENDER",&argument) || demo_command_arg(line,"NUM",&argument) ||
    demo_command_arg(line,"SPD",&argument) || demo_command_arg(line,"LEVEL",&argument) ||
    demo_command_arg(line,"COLLIDE",&argument) || demo_command_arg(line,"ASYNC",&argument)){
  game_text_reply("ERR UNSUPPORTED GAME_ONLY\r\n");return;
 }
 if(demo_text_equal(line,"STATUS")){demo_uart_status();return;}
 peer=demo_uart_protocol_active() || bg_ddr_adapter_active() || game_lifecycle_busy();
 /* Inactive EXIT must not claim that an unfinished boot seed has recovered. */
 if(demo_text_equal(line,"ENEMY EXIT") && !enemy_test_active() && !game_lifecycle_safe()){
  int r=game_inactive_exit();
  uart_reply_event(r<0?"ENEMY ERROR NOT_IDLE\r\n":"ENEMY EXIT ACCEPTED\r\n");return;
 }
 if(demo_uart_protocol_active()?enemy_test_handle_protocol_line(line,peer):enemy_test_handle_line(line,peer))return;
 if(enemy_test_active()){
  if(demo_command_arg(line,"ASSET",&argument) && demo_text_equal(argument,"STATUS"))asset_upload_handle_line(line);
  else if(demo_command_arg(line,"BG",&argument) && (demo_text_equal(argument,"STATUS") || demo_text_equal(argument,"HELLO")))bg_uart_handle_line(line);
  else game_text_reply("ERR ENEMY_BUSY\r\n");
  return;
 }
 if(demo_command_arg(line,"ASSET",&argument) && demo_command_arg(argument,"BEGIN",&argument) &&
    (game_lifecycle_busy() || bg_ddr_adapter_active() || bg_uart_preparing() || bg_uart_recovery_active())){
  game_text_reply("ASSET ERROR BUSY\r\n");return;
 }
 if(asset_upload_handle_line(line))return;
 if(asset_upload_active()){
  if(demo_command_arg(line,"BG",&argument) && (demo_text_equal(argument,"STATUS") || demo_text_equal(argument,"HELLO")))bg_uart_handle_line(line);
  else game_text_reply("ERR ASSET_BUSY\r\n");return;
 }
 if(bg_uart_handle_line(line))return;
 game_text_reply("ERR UNKNOWN_COMMAND\r\n");
}
/* Stage-1 D: every non-input command records wait-to-start, CPU time and the
 * reply bytes already queued ahead of it. Input lines stay untimed. */
static void demo_uart_run_line(const char *line,u64 ready_at){
 u64 start,end;unsigned before,heavy;
 if(enemy_test_input_line(line)){demo_uart_apply_line(line);return;}
 heavy=enemy_test_is_heavy_query(demo_skip_spaces(line));before=uart_reply_queued();start=clint_getTime(BSP_CLINT);
 demo_uart_apply_line(line);end=clint_getTime(BSP_CLINT);
 enemy_test_diag_command(heavy,ready_at,start,end,before,uart_reply_queued());
}
static unsigned demo_uart_line_pending;

/* One deferred read-only query allows following silent input through response

 * backpressure. A second non-input line retains the original ordered stall.
 * O2b: the same slot also takes a complete heavy query while the frame
 * pipeline is busy; that wait is capped at DEMO_UART_DEFER_TICKS (20 ms),
 * after which only reply space can delay it, as before. A complete ENEMY
 * STOP/EXIT behind it is applied at once and its reply is held until the
 * deferred query is queued, so replies keep command order. */
#define DEMO_UART_DEFER_TICKS ((u64)BSP_CLINT_HZ*20u/1000u)

static char demo_uart_deferred_status[DEMO_UART_LINE_SIZE];

static unsigned demo_uart_deferred_status_pending;

static u64 demo_uart_deferred_status_at;

static int demo_uart_is_status(const char *line)

{

    line = demo_skip_spaces(line);

    return demo_text_equal(line,"STATUS") || demo_text_equal(line,"ENEMY STATUS");

}

static void demo_uart_slot_take(void)
{
    memcpy(demo_uart_deferred_status,demo_uart_line,demo_uart_line_length+1u);
    demo_uart_deferred_status_at = demo_uart_line_done_at;
    demo_uart_deferred_status_pending = 1u;
}

static void demo_uart_dispatch_pending(void)

{

    if (demo_uart_deferred_status_pending) {
        int busy = enemy_test_query_busy();
        if ((!busy || clint_getTime(BSP_CLINT) - demo_uart_deferred_status_at >= DEMO_UART_DEFER_TICKS) &&
            demo_uart_line_ready(demo_uart_deferred_status)) {
            if (busy) enemy_test_diag_event(EN_DIAG_TIMEOUT, 0u);
            uart_reply_direct(1u);
            demo_uart_run_line(demo_uart_deferred_status,demo_uart_deferred_status_at);
            uart_reply_direct(0u);
            demo_uart_deferred_status_pending = 0u;
        }
    }

    if (!demo_uart_deferred_status_pending) (void)uart_reply_hold_release();

    if (!demo_uart_line_pending) return;

    if (!demo_uart_line_overflow && enemy_test_input_line(demo_uart_line)) {

        /* Silent complete-state input passes a deferred query and held replies. */
        demo_uart_run_line(demo_uart_line,demo_uart_line_done_at);

    } else if (!demo_uart_line_overflow && !demo_uart_protocol_active() &&
               enemy_test_is_priority_line(demo_uart_line) &&
               (demo_uart_deferred_status_pending || uart_reply_holding() ||
                !demo_uart_line_ready(demo_uart_line))) {

        /* Apply STOP/EXIT now; its reply waits behind the not-yet-run query. */
        if (!uart_reply_hold_room()) return;
        uart_reply_hold_enter();
        demo_uart_run_line(demo_uart_line,demo_uart_line_done_at);
        enemy_test_diag_event(EN_DIAG_PRIORITY, uart_reply_hold_bytes());
        if (!demo_uart_deferred_status_pending) (void)uart_reply_hold_release();

    } else if (demo_uart_deferred_status_pending || uart_reply_holding()) {

        return;

    } else if (demo_uart_line_overflow) {

        if (demo_uart_protocol_active()) {

            /* Keep protocol diagnostics out of the suspended text queue. */

            uart_writeStr(BSP_UART_TERMINAL, "ERR OVERFLOW\r\n");

        } else {

            if (!uart_reply_query_ready()) return;

            uart_reply_event("ERR OVERFLOW\r\n");

        }

    } else {

        int heavy = !demo_uart_protocol_active() &&
                    enemy_test_is_heavy_query(demo_skip_spaces(demo_uart_line));

        if (heavy && enemy_test_query_busy()) {

            demo_uart_slot_take();
            enemy_test_diag_event(EN_DIAG_DEFERRED, 0u);

        } else if (!demo_uart_line_ready(demo_uart_line)) {

            if (!demo_uart_protocol_active() && (heavy || demo_uart_is_status(demo_uart_line))) {

                demo_uart_slot_take();

                demo_uart_line_pending = demo_uart_line_length = demo_uart_line_overflow = 0u;

            }

            return;

        } else {

            demo_uart_run_line(demo_uart_line,demo_uart_line_done_at);

        }

    }

    demo_uart_line_pending = demo_uart_line_length = demo_uart_line_overflow = 0u;

}



static void demo_uart_poll(void)

{

    u32 budget = DEMO_UART_POLL_BUDGET;

    int resource_guard = renderer_cpu_sync_waiting();



    enemy_test_input_service();



    /* KEY rejection was accepted at a protocol text boundary. Keep the next

     * READY/ACK/payload behind its nonblocking response, never interleave. */

    if (enemy_test_service_protocol_reply()) return;



    bg_uart_set_resource_guard(resource_guard || asset_upload_active() || enemy_test_active());

    asset_upload_set_resource_guard(resource_guard);

    if (resource_guard)

        bg_uart_service_protocol_only();

    else

        bg_uart_service();

    asset_upload_service();

    enemy_test_service(resource_guard);

    if (!resource_guard) enemy_test_app_service(demo_uart_protocol_active() || bg_ddr_adapter_active() || game_lifecycle_busy());

    game_hud_service();

    if (!demo_uart_protocol_active()) uart_reply_service();

    enemy_test_diag_tx_poll();

    if (demo_uart_deferred_status_pending || uart_reply_holding()) demo_uart_dispatch_pending();



    /* Do not drain the UART while a synchronous writer callback owns the

     * CPU.  This preserves bytes for the main-loop asset service and prevents

     * an ASSET packet from entering a nested DDR write transaction. */

    if (resource_guard && asset_upload_active()) {

        bg_uart_set_resource_guard(0);

        asset_upload_set_resource_guard(0);

        return;

    }



    if (demo_uart_line_pending) {

        demo_uart_dispatch_pending();

        budget = 0u;

    }

    while ((budget != 0u)) {

        uint8_t character;

        if (!bg_uart_read_byte(&character))

            break;

        budget--;



        /* This remains the only physical UART reader.  Once CHECK has

         * accepted its command, every byte, including CR/LF/CAN and command

         * looking payload, belongs to the BG state machine. */

        if (bg_uart_binary_active() ||

            bg_uart_recovery_active() ||

            (bg_uart_replay_active() &&

             ((character == 0x04u) || (character == 0x18u))) ||

            (bg_uart_preparing() && (character == 0x18u))) {

            (void)bg_uart_consume_byte(character);

            continue;

        }

        if (asset_upload_route_byte(character)) {

            (void)asset_upload_consume_byte(character);

            continue;

        }



        if ((character == '\r') || (character == '\n')) {

            if (demo_uart_line_overflow || demo_uart_line_length) {

                demo_uart_line[demo_uart_line_length] = '\0';

                demo_uart_line_done_at = clint_getTime(BSP_CLINT);

                demo_uart_line_pending = 1u;

                demo_uart_dispatch_pending();

                if (demo_uart_line_pending || enemy_test_protocol_reply_pending()) budget = 0u;

            }

        } else if (demo_uart_line_overflow == 0u) {

            if (demo_uart_line_length < (DEMO_UART_LINE_SIZE - 1u)) {

                demo_uart_line[demo_uart_line_length++] = (char)character;

            } else {

                demo_uart_line_overflow = 1u;

            }

        }

    }

    bg_uart_set_resource_guard(0);

    asset_upload_set_resource_guard(0);

}



void main(void){
 const enemy_test_hooks_t hooks={game_prepare_surface,game_surface_snapshot,game_request_idle,game_idle_confirmed};
 bsp_init();enemy_test_init(&hooks);asset_upload_init();
 asset_upload_set_hooks(game_asset_prepare,game_asset_resume);
 bg_uart_init();bg_uart_set_hooks(game_bg_prepare,game_bg_restore);
 bg_uart_set_restore_status_hook(game_bg_restore_status);bg_uart_set_restore_pending_hook(game_bg_restore_pending);
 renderer_cpu_set_target(GPU_TARGET_FULL_CPU);renderer_cpu_set_service(demo_uart_poll);
 game_lifecycle_init(demo_uart_poll);
 uart_writeStr(BSP_UART_TERMINAL,"\r\nBUILD: GAME_ONLY_D8_G1 P1000_20261026; waiting for confirmed idle\r\n");
 for(;;){demo_uart_poll();game_lifecycle_service();}
}
#endif
