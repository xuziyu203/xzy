#ifndef BUILD_VARIANT_GAME /* GHD3: full UART build only (single ELF: game_noserial.c stubs) */
#include "bg_uart.h"
#include "bg_ddr_adapter.h"
#include "bsp.h"
#include <stddef.h>
#include <string.h>

#define BG_LINE_MAX              96u
#define BG_BLOCK_SIZE            128u
#define BG_CHECK_MAX             3840u
#define BG_START_TIMEOUT_SEC     15u
#define BG_PACKET_TIMEOUT_SEC    2u
#define BG_CANCEL_COUNT          8u

#define BG_SOH                   0x01u
#define BG_EOT                   0x04u
#define BG_ACK                   0x06u
#define BG_NAK                   0x15u
#define BG_CAN                   0x18u
#define BG_CRC_REQUEST           0x43u

typedef enum {
    BG_IDLE = 0,
    BG_WAIT_PACKET,
    BG_PACKET,
    BG_COMPLETE,
    BG_PREPARING,
    BG_FRAME_WAIT_PACKET,
    BG_FRAME_STORED,
    BG_FRAME_SHOWN
} bg_state_t;

static bg_state_t bg_state;
static uint8_t bg_recovery;
static uint8_t bg_packet[BG_BLOCK_SIZE];
static uint8_t bg_previous[BG_BLOCK_SIZE];
static uint8_t bg_have_previous;
static uint8_t bg_expected_seq;
static uint8_t bg_packet_seq;
static uint8_t bg_packet_inverse;
static uint8_t bg_packet_crc_hi;
static uint16_t bg_packet_pos;
static uint8_t bg_cancel_count;
static uint32_t bg_expected_bytes;
static uint32_t bg_received_bytes;
static uint32_t bg_expected_crc;
static uint32_t bg_crc_state;
static uint32_t bg_last_error;
static u64 bg_last_activity;
static char bg_recovery_line[BG_LINE_MAX];
static uint32_t bg_recovery_line_len;
static char bg_frame_line[BG_LINE_MAX];
static uint32_t bg_frame_line_len;
static int (*bg_prepare_hook)(void);
static int (*bg_restore_hook)(void);
static int (*bg_restore_status_hook)(void);
static int (*bg_restore_pending_hook)(void);
static uint32_t bg_saved_length;
static uint32_t bg_saved_crc;
/* Remains set while XMODEM packets belong to a full-frame transfer. */
static uint8_t bg_frame_mode;
static uint8_t bg_cancel_pending;
/* A synchronous renderer wait may still parse protocol bytes, but it cannot
 * enter adapter/restore/abort code.  A single deferred abort bit is enough;
 * this is not a second protocol queue or a second state machine. */
static uint8_t bg_resource_guard;
static uint8_t bg_abort_pending;
typedef enum {
    BG_EXIT_NONE = 0,
    BG_EXIT_RESTORING,
    BG_EXIT_RESTORED,
    BG_EXIT_FAILED
} bg_exit_phase_t;
static bg_exit_phase_t bg_exit_phase;

static void bg_write_text(const char *text)
{
    uart_writeStr(BSP_UART_TERMINAL, text);
}

static void bg_write_u32(uint32_t value)
{
    char digits[10];
    uint32_t count = 0u;
    do {
        digits[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    } while (value != 0u);
    while (count != 0u)
        uart_write(BSP_UART_TERMINAL, digits[--count]);
}

static void bg_write_i32(int value)
{
    uint32_t magnitude;
    if (value < 0) {
        uart_write(BSP_UART_TERMINAL, '-');
        magnitude = (uint32_t)(-(value + 1)) + 1u;
        bg_write_u32(magnitude);
    } else {
        bg_write_u32((uint32_t)value);
    }
}

static void bg_write_hex32(uint32_t value)
{
    static const char hex[] = "0123456789ABCDEF";
    int shift;
    for (shift = 28; shift >= 0; shift -= 4)
        uart_write(BSP_UART_TERMINAL, hex[(value >> (uint32_t)shift) & 0xFu]);
}

static u64 bg_now(void)
{
    return clint_getTime(BSP_CLINT);
}

static int bg_elapsed(u64 now, u64 start, u64 limit)
{
    return (now >= start) && ((now - start) >= limit);
}

static int bg_resources_blocked(void)
{
    return bg_resource_guard != 0u;
}

static uint16_t bg_crc16(const uint8_t *data, uint32_t length)
{
    uint16_t crc = 0u;
    uint32_t i;
    for (i = 0u; i < length; ++i) {
        uint8_t bit;
        crc ^= (uint16_t)data[i] << 8;
        for (bit = 0u; bit < 8u; ++bit)
            crc = (uint16_t)((crc & 0x8000u) ?
                             (uint16_t)((crc << 1) ^ 0x1021u) :
                             (uint16_t)(crc << 1));
    }
    return crc;
}

static uint32_t bg_crc32_update(uint32_t crc, uint8_t byte)
{
    uint8_t bit;
    crc ^= byte;
    for (bit = 0u; bit < 8u; ++bit)
        crc = (crc & 1u) ? ((crc >> 1) ^ 0xEDB88320u) : (crc >> 1);
    return crc;
}

static int bg_token(const char **cursor, char *out, uint32_t out_size)
{
    const char *p = *cursor;
    uint32_t length = 0u;
    while (*p == ' ' || *p == '\t') ++p;
    if (*p == '\0') return 0;
    while (p[length] != '\0' && p[length] != ' ' &&
           p[length] != '\t' && p[length] != '\r' && p[length] != '\n') {
        if (length + 1u >= out_size) return -1;
        out[length] = p[length];
        ++length;
    }
    out[length] = '\0';
    p += length;
    *cursor = p;
    return 1;
}

static int bg_token_end(const char *cursor)
{
    while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' ||
           *cursor == '\n') ++cursor;
    return *cursor == '\0';
}

static int bg_parse_decimal(const char *text, uint32_t *value)
{
    uint32_t result = 0u;
    uint32_t i = 0u;
    if (text[0] == '\0') return 0;
    while (text[i] != '\0') {
        uint32_t digit;
        if (text[i] < '0' || text[i] > '9') return 0;
        digit = (uint32_t)(text[i] - '0');
        if (result > (0xFFFFFFFFu - digit) / 10u) return 0;
        result = result * 10u + digit;
        ++i;
    }
    *value = result;
    return 1;
}

static int bg_parse_hex8(const char *text, uint32_t *value)
{
    uint32_t result = 0u;
    uint32_t i;
    for (i = 0u; i < 9u; ++i) {
        if (text[i] == '\0')
            break;
    }
    if (i != 8u)
        return 0;
    for (i = 0u; i < 8u; ++i) {
        uint8_t c = (uint8_t)text[i];
        uint32_t digit;
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10u;
        else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10u;
        else return 0;
        result = (result << 4) | digit;
    }
    *value = result;
    return 1;
}

static void bg_error(uint32_t code)
{
    if (bg_state >= BG_PREPARING && bg_ddr_adapter_active()) {
        if (bg_resources_blocked())
            bg_abort_pending = 1u;
        else
            (void)bg_ddr_adapter_abort();
    }
    bg_last_error = code;
    bg_state = BG_IDLE;
    bg_recovery = 1u;
    bg_recovery_line_len = 0u;
    uart_write(BSP_UART_TERMINAL, (char)BG_CAN);
    uart_write(BSP_UART_TERMINAL, (char)BG_CAN);
    bg_write_text("\r\nBG ERROR ");
    bg_write_u32(code);
    bg_write_text("\r\n");
}

static void bg_checked(void)
{
    uint32_t crc = bg_crc_state ^ 0xFFFFFFFFu;
    if (crc != bg_expected_crc) {
        bg_error(5u);
        return;
    }
    uart_write(BSP_UART_TERMINAL, (char)BG_ACK);
    bg_write_text("BG CHECKED ");
    bg_write_u32(bg_received_bytes);
    bg_write_text(" ");
    bg_write_hex32(crc);
    bg_write_text("\r\n");
    bg_state = BG_COMPLETE;
    bg_last_error = 0u;
    bg_last_activity = bg_now();
}

static void bg_start_check(uint32_t length, uint32_t crc)
{
    bg_frame_mode = 0u;
    bg_state = BG_WAIT_PACKET;
    bg_recovery = 0u;
    bg_expected_bytes = length;
    bg_expected_crc = crc;
    bg_received_bytes = 0u;
    bg_crc_state = 0xFFFFFFFFu;
    bg_expected_seq = 1u;
    bg_have_previous = 0u;
    bg_packet_pos = 0u;
    bg_cancel_count = 0u;
    bg_last_error = 0u;
    bg_last_activity = bg_now();
    bg_write_text("BG READY\r\n");
    uart_write(BSP_UART_TERMINAL, (char)BG_CRC_REQUEST);
}

static void bg_start_frame(uint32_t length, uint32_t crc)
{
    bg_ddr_adapter_diag_show_reset();
    bg_frame_mode = 1u;
    bg_saved_length = length;
    bg_saved_crc = crc;
    bg_state = BG_PREPARING;
    bg_recovery = 0u;
    bg_last_error = 0u;
    bg_expected_bytes = length;
    bg_expected_crc = crc;
    bg_received_bytes = 0u;
    bg_crc_state = 0xFFFFFFFFu;
    bg_expected_seq = 1u;
    bg_have_previous = 0u;
    bg_packet_pos = 0u;
    bg_cancel_count = 0u;
    bg_frame_line_len = 0u;
    bg_exit_phase = BG_EXIT_NONE;
    bg_last_activity = bg_now();
}

void bg_uart_init(void)
{
    bg_state = BG_IDLE;
    bg_frame_mode = 0u;
    bg_cancel_pending = 0u;
    bg_resource_guard = 0u;
    bg_abort_pending = 0u;
    bg_exit_phase = BG_EXIT_NONE;
    bg_recovery = 0u;
    bg_last_error = 0u;
    bg_recovery_line_len = 0u;
    bg_frame_line_len = 0u;
    bg_ddr_adapter_init();
}

void bg_uart_set_resource_guard(int blocked)
{
    bg_resource_guard = blocked ? 1u : 0u;
}

void bg_uart_set_hooks(int (*prepare_hook)(void), int (*restore_hook)(void))
{
    bg_prepare_hook = prepare_hook;
    bg_restore_hook = restore_hook;
}

void bg_uart_set_restore_status_hook(int (*status_hook)(void))
{
    bg_restore_status_hook = status_hook;
}

void bg_uart_set_restore_pending_hook(int (*pending_hook)(void))
{
    bg_restore_pending_hook = pending_hook;
}

int bg_uart_binary_active(void)
{
    return bg_state == BG_WAIT_PACKET || bg_state == BG_PACKET ||
           bg_state == BG_FRAME_WAIT_PACKET;
}

int bg_uart_preparing(void) { return bg_state == BG_PREPARING; }

int bg_uart_recovery_active(void)
{
    return bg_recovery != 0u || bg_state == BG_FRAME_STORED ||
           bg_state == BG_FRAME_SHOWN;
}

int bg_uart_replay_active(void) { return bg_state == BG_COMPLETE; }


/* The only function in the BG module that reads the UART hardware.  Both the
 * main-loop dispatcher and a blocking writer wait use it, so a writer wait
 * cannot create a second physical UART reader. */
int bg_uart_read_byte(uint8_t *byte)
{
    if ((byte == 0) || (uart_readOccupancy(BSP_UART_TERMINAL) == 0u))
        return 0;
    *byte = (uint8_t)uart_read(BSP_UART_TERMINAL);
    return 1;
}

/* A row write may wait for DDR while the sender is able to cancel.  Drain
 * already queued bytes without parsing them recursively; the outer parser
 * handles the resulting cancellation after the writer returns. */
void bg_uart_service_during_write(void)
{
    uint32_t budget = 64u;
    uint8_t byte;
    while ((budget != 0u) && bg_uart_read_byte(&byte)) {
        if (byte == BG_CAN)
            bg_cancel_pending = 1u;
        --budget;
    }
}

int bg_uart_take_cancel(void)
{
    int pending = (bg_cancel_pending != 0u);
    bg_cancel_pending = 0u;
    return pending;
}

static int bg_line_is_bg(const char *line)
{
    return line[0] == 'B' && line[1] == 'G' &&
           (line[2] == '\0' || line[2] == ' ' || line[2] == '\t');
}

int bg_uart_handle_line(const char *line)
{
    const char *cursor;
    char token[24];
    char token2[24];
    char token3[24];
    char token4[24];
    char token5[24];
    char token6[24];
    uint32_t length;
    uint32_t crc;
    uint32_t width;
    uint32_t height;
    int result;

    if (!bg_line_is_bg(line)) return 0;
    cursor = line + 2;
    result = bg_token(&cursor, token, sizeof(token));
    if (result <= 0) {
        bg_write_text("BG ERROR SYNTAX\r\n");
        return 1;
    }
    if (strcmp(token, "HELLO") == 0 && bg_token_end(cursor)) {
        bg_write_text("BG HELLO 1 RGB565LE 1920 1080 XMODEM128_CRC CHECK");
        if (bg_prepare_hook != 0)
            bg_write_text(",FRAME");
        bg_write_text("\r\n");
        return 1;
    }
    if (strcmp(token, "STATUS") == 0 && bg_token_end(cursor)) {
        bg_write_text("BG STATUS STATE=");
        if (bg_exit_phase == BG_EXIT_RESTORING)
            bg_write_text("RESTORING");
        else if (bg_exit_phase == BG_EXIT_FAILED)
            bg_write_text("RESTORE_FAILED");
        else if (bg_exit_phase == BG_EXIT_RESTORED)
            bg_write_text("RESTORED");
        else if (bg_state == BG_PREPARING)
            bg_write_text("PREPARING");
        else if (bg_uart_binary_active())
            bg_write_text("RECEIVING");
        else if (bg_state == BG_FRAME_STORED)
            bg_write_text(bg_ddr_adapter_error() ? "SHOW_FAILED" : "READY_TO_SHOW");
        else if (bg_state == BG_FRAME_SHOWN)
            bg_write_text("DISPLAYED");
        else if (bg_recovery)
            bg_write_text("RECOVERY");
        else if (bg_state == BG_COMPLETE)
            bg_write_text("CHECKED");
        else
            bg_write_text("IDLE");
        bg_write_text(" RX="); bg_write_u32(bg_received_bytes);
        bg_write_text(" CRC="); bg_write_hex32(bg_crc_state ^ 0xFFFFFFFFu);
        bg_write_text(" ERROR="); bg_write_u32(bg_last_error);
        bg_write_text(" ROW="); bg_write_u32(bg_ddr_adapter_row());
        bg_write_text(" FRONT=FULL_CPU BACK=FULL_CPU");
        bg_write_text(" PRESENT=NOT_RUN DDR_READBACK=NOT_RUN BEGIN_RC=");
        bg_write_i32(bg_ddr_adapter_diag_begin_rc());
        bg_write_text(" BEGIN_STATE=");
        bg_write_u32((uint32_t)bg_ddr_adapter_diag_begin_state());
        bg_write_text(" WR_ERR=");
        if (bg_ddr_adapter_diag_writer_error_valid())
            bg_write_i32(bg_ddr_adapter_diag_writer_error());
        else
            bg_write_text("NA");
        bg_write_text(" WR_STATUS=");
        if (bg_ddr_adapter_diag_writer_status_valid())
            bg_write_hex32(bg_ddr_adapter_diag_writer_status());
        else
            bg_write_text("NA");
        bg_write_text(" BORROW_BLOCK=");
        if (bg_ddr_adapter_diag_borrow_valid())
            bg_write_u32(bg_ddr_adapter_diag_borrow_block());
        else
            bg_write_text("NA");
        bg_write_text(" SHOW_VALID=");
        bg_write_u32((uint32_t)bg_ddr_adapter_diag_show_valid());
        bg_write_text(" SHOW_STATE=");
        if (bg_ddr_adapter_diag_show_valid())
            bg_write_i32(bg_ddr_adapter_diag_show_state());
        else bg_write_text("NA");
        bg_write_text(" SHOW_RC=");
        if (bg_ddr_adapter_diag_show_valid())
            bg_write_i32(bg_ddr_adapter_diag_show_rc());
        else bg_write_text("NA");
        bg_write_text(" SHOW_ERR=");
        if (bg_ddr_adapter_diag_show_valid())
            bg_write_u32(bg_ddr_adapter_diag_show_rc() < 0);
        else bg_write_text("NA");
        bg_write_text(" END_ISSUED=");
        if (bg_ddr_adapter_diag_show_valid())
            bg_write_u32((uint32_t)bg_ddr_adapter_diag_show_end_issued());
        else bg_write_text("NA");
        bg_write_text(" SHOW_WR_ERR=");
        if (bg_ddr_adapter_diag_show_writer_error_valid())
            bg_write_i32(bg_ddr_adapter_diag_show_writer_error());
        else bg_write_text("NA");
        bg_write_text(" SHOW_WR_STATUS=");
        if (bg_ddr_adapter_diag_show_writer_status_valid())
            bg_write_hex32(bg_ddr_adapter_diag_show_writer_status());
        else bg_write_text("NA");
        bg_write_text(" SHOW_WAIT=");
        if (bg_ddr_adapter_diag_show_writer_error_valid())
            bg_write_u32(bg_ddr_adapter_diag_show_writer_stage());
        else bg_write_text("NA");
        bg_write_text(" SHOW_GEN_SAVED=");
        if (bg_ddr_adapter_diag_show_gen_saved_valid())
            bg_write_hex32(bg_ddr_adapter_diag_show_gen_saved());
        else bg_write_text("NA");
        bg_write_text(" SHOW_GEN_NOW=");
        if (bg_ddr_adapter_diag_show_gen_now_valid())
            bg_write_hex32(bg_ddr_adapter_diag_show_gen_now());
        else bg_write_text("NA");
        bg_write_text(" SHOW_DISPLAY=");
        if (bg_ddr_adapter_diag_show_display_valid())
            bg_write_hex32(bg_ddr_adapter_diag_show_display());
        else bg_write_text("NA");
        bg_write_text("\r\n");
        return 1;
    }
    if (strcmp(token, "BEGIN") == 0) {
        if (bg_prepare_hook == 0) {
            bg_write_text("BG ERROR UNSUPPORTED\r\n");
            return 1;
        }
        result = bg_token(&cursor, token2, sizeof(token2));
        if (result <= 0 || strcmp(token2, "1") != 0 ||
            bg_token(&cursor, token3, sizeof(token3)) <= 0 ||
            bg_token(&cursor, token4, sizeof(token4)) <= 0 ||
            bg_token(&cursor, token5, sizeof(token5)) <= 0 ||
            bg_token(&cursor, token6, sizeof(token6)) <= 0 ||
            !bg_token_end(cursor) || !bg_parse_decimal(token3, &width) ||
            !bg_parse_decimal(token4, &height) ||
            !bg_parse_decimal(token5, &length) ||
            !bg_parse_hex8(token6, &crc) || width != 1920u ||
            height != 1080u || length != 4147200u) {
            bg_write_text("BG ERROR BAD_BEGIN\r\n");
            return 1;
        }
        if (((bg_state != BG_IDLE) && (bg_state != BG_COMPLETE)) || bg_recovery ||
            (bg_exit_phase == BG_EXIT_RESTORING) ||
            (bg_exit_phase == BG_EXIT_FAILED) ||
            bg_ddr_adapter_active()) {
            bg_write_text("BG ERROR BUSY\r\n");
            return 1;
        }
        bg_start_frame(length, crc);
        return 1;
    }
    if (strcmp(token, "SHOW") == 0) {
        if (bg_state == BG_FRAME_SHOWN) {
            bg_write_text("BG SHOWN\r\n");
            return 1;
        }
        if (bg_resources_blocked()) {
            bg_write_text("BG ERROR BUSY\r\n");
            return 1;
        }
        if (bg_state != BG_FRAME_STORED || bg_ddr_adapter_show() != 0) {
            bg_write_text("BG ERROR RECOVERY_REQUIRED\r\n");
            return 1;
        }
        bg_state = BG_FRAME_SHOWN;
        bg_write_text("BG SHOWN\r\n");
        return 1;
    }
    if (strcmp(token, "ABORT") == 0 || strcmp(token, "EXIT") == 0) {
        if (bg_resources_blocked()) {
            bg_write_text("BG ERROR BUSY\r\n");
            return 1;
        }
        if (strcmp(token, "EXIT") == 0 &&
            (bg_exit_phase == BG_EXIT_RESTORING ||
             bg_exit_phase == BG_EXIT_RESTORED)) {
            bg_write_text(bg_exit_phase == BG_EXIT_RESTORED ?
                          "BG EXITED\r\n" : "BG RESTORING\r\n");
            return 1;
        }
        if (strcmp(token, "EXIT") == 0 && bg_exit_phase == BG_EXIT_FAILED) {
            int retry_result;
            if (bg_restore_hook == 0) {
                bg_write_text("BG ERROR RESTORE_FAILED\r\n");
                return 1;
            }
            retry_result = bg_restore_hook();
            if (retry_result != 0) {
                bg_write_text("BG ERROR RESTORE_FAILED\r\n");
                return 1;
            }
            if (bg_ddr_adapter_shown() && bg_ddr_adapter_exit() != 0) {
                bg_write_text("BG ERROR RESTORE_FAILED\r\n");
                return 1;
            }
            bg_exit_phase = BG_EXIT_RESTORING;
            bg_write_text("BG RESTORING\r\n");
            return 1;
        }
        if (strcmp(token, "EXIT") == 0 &&
            (bg_state == BG_FRAME_STORED || bg_state == BG_FRAME_WAIT_PACKET ||
             bg_state == BG_PACKET || bg_state == BG_PREPARING)) {
            bg_write_text("BG ERROR RECOVERY_REQUIRED\r\n");
            return 1;
        }
        if (strcmp(token, "ABORT") == 0 && bg_ddr_adapter_active()) {
            if (bg_ddr_adapter_shown() || bg_ddr_adapter_abort() != 0) {
                bg_write_text("BG ERROR RECOVERY_REQUIRED\r\n");
                return 1;
            }
            bg_state = BG_IDLE;
            bg_recovery = 0u;
            bg_last_error = 0u;
            bg_write_text("BG ABORTED\r\n");
            return 1;
        }
        if (bg_state >= BG_PREPARING && bg_ddr_adapter_active())
            (void)bg_ddr_adapter_abort();
        if (strcmp(token, "EXIT") == 0 &&
            (bg_ddr_adapter_shown() ||
             (bg_restore_pending_hook != 0 && bg_restore_pending_hook()))) {
            int restore_result;
            /* A failed abort can still own the writer/row. Prove that this
             * non-presented transfer drained before registering demo work. */
            if (!bg_ddr_adapter_shown() && bg_ddr_adapter_active() &&
                bg_ddr_adapter_exit() != 0) {
                bg_write_text("BG ERROR RECOVERY_REQUIRED\r\n");
                return 1;
            }
            if (bg_exit_phase == BG_EXIT_RESTORING) {
                bg_write_text("BG RESTORING\r\n");
                return 1;
            }
            if (bg_exit_phase == BG_EXIT_RESTORED) {
                bg_write_text("BG EXITED\r\n");
                return 1;
            }
            if (bg_restore_hook != 0) {
                restore_result = bg_restore_hook();
                if (restore_result > 0) {
                    /* An in-flight request is already registered. */
                    bg_exit_phase = BG_EXIT_RESTORING;
                    bg_write_text("BG RESTORING\r\n");
                    return 1;
                }
                if (restore_result != 0 || bg_ddr_adapter_exit() != 0) {
                    bg_exit_phase = BG_EXIT_FAILED;
                    bg_write_text("BG ERROR RESTORE_FAILED\r\n");
                    return 1;
                }
                bg_exit_phase = BG_EXIT_RESTORING;
                bg_state = BG_IDLE;
                bg_recovery = 0u;
                bg_last_error = 0u;
                bg_write_text("BG RESTORING\r\n");
                return 1;
            }
            (void)bg_ddr_adapter_exit();
        } else if (strcmp(token, "EXIT") == 0 && bg_ddr_adapter_active()) {
            if (bg_ddr_adapter_exit() != 0) {
                bg_write_text("BG ERROR RECOVERY_REQUIRED\r\n");
                return 1;
            }
        }
        bg_state = BG_IDLE;
        bg_recovery = 0u;
        bg_last_error = 0u;
        bg_write_text(strcmp(token, "ABORT") == 0 ?
                      "BG ABORTED\r\n" : "BG EXITED\r\n");
        return 1;
    }
    if (strcmp(token, "CHECK") != 0) {
        bg_write_text("BG ERROR UNSUPPORTED\r\n");
        return 1;
    }
    result = bg_token(&cursor, token2, sizeof(token2));
    if (result <= 0 || strcmp(token2, "1") != 0 ||
        bg_token(&cursor, token3, sizeof(token3)) <= 0 ||
        bg_token(&cursor, token4, sizeof(token4)) <= 0 ||
        !bg_token_end(cursor) || !bg_parse_decimal(token3, &length) ||
        !bg_parse_hex8(token4, &crc) || length < BG_BLOCK_SIZE ||
        length > BG_CHECK_MAX || (length % BG_BLOCK_SIZE) != 0u) {
        bg_write_text("BG ERROR BAD_CHECK\r\n");
        return 1;
    }
    if (bg_uart_binary_active() || bg_uart_recovery_active() || bg_recovery ||
        bg_uart_preparing() || bg_ddr_adapter_active() ||
        bg_exit_phase == BG_EXIT_RESTORING || bg_exit_phase == BG_EXIT_FAILED ||
        (bg_restore_pending_hook != 0 && bg_restore_pending_hook())) {
        bg_write_text("BG ERROR BUSY\r\n");
        return 1;
    }
    bg_start_check(length, crc);
    return 1;
}

static void bg_accept_packet(void)
{
    uint32_t i;
    if (bg_received_bytes + BG_BLOCK_SIZE > bg_expected_bytes) {
        bg_error(7u);
        return;
    }
    for (i = 0u; i < BG_BLOCK_SIZE; ++i) {
        bg_previous[i] = bg_packet[i];
        bg_crc_state = bg_crc32_update(bg_crc_state, bg_packet[i]);
    }
    bg_have_previous = 1u;
    bg_received_bytes += BG_BLOCK_SIZE;
    bg_expected_seq = (uint8_t)(bg_expected_seq + 1u);
    bg_last_activity = bg_now();
    uart_write(BSP_UART_TERMINAL, (char)BG_ACK);
    bg_state = (bg_state == BG_FRAME_WAIT_PACKET) ?
               BG_FRAME_WAIT_PACKET : BG_WAIT_PACKET;
}

int bg_uart_consume_byte(uint8_t byte)
{
    uint16_t crc_received;
    uint16_t crc_calculated;
    uint8_t previous_seq;

    if ((bg_state == BG_COMPLETE) && (byte == BG_EOT)) {
        uart_write(BSP_UART_TERMINAL, (char)BG_ACK);
        bg_write_text("BG CHECKED "); bg_write_u32(bg_received_bytes);
        bg_write_text(" "); bg_write_hex32(bg_crc_state ^ 0xFFFFFFFFu);
        bg_write_text("\r\n");
        return 1;
    }

    if (bg_state == BG_FRAME_STORED || bg_state == BG_FRAME_SHOWN) {
        if (byte == '\r' || byte == '\n') {
            if (bg_frame_line_len != 0u) {
                bg_frame_line[bg_frame_line_len] = '\0';
                if (!bg_uart_handle_line(bg_frame_line))
                    bg_write_text("ERR BG_BUSY\r\n");
                bg_frame_line_len = 0u;
            }
        } else if (byte >= 0x20u && byte <= 0x7Eu &&
                   bg_frame_line_len + 1u < BG_LINE_MAX) {
            bg_frame_line[bg_frame_line_len++] = (char)byte;
        } else {
            bg_frame_line_len = 0u;
        }
        return 1;
    }

    if (bg_recovery) {
        if (byte == '\r' || byte == '\n') {
            if (bg_recovery_line_len != 0u) {
                bg_recovery_line[bg_recovery_line_len] = '\0';
                if (bg_line_is_bg(bg_recovery_line)) {
                    bg_uart_handle_line(bg_recovery_line);
                }
                bg_recovery_line_len = 0u;
            }
        } else if (byte >= 0x20u && byte <= 0x7Eu &&
                   bg_recovery_line_len + 1u < BG_LINE_MAX) {
            bg_recovery_line[bg_recovery_line_len++] = (char)byte;
        } else {
            bg_recovery_line_len = 0u;
        }
        return 1;
    }
    if (bg_state == BG_PREPARING) {
        if (byte == BG_CAN) {
            if (++bg_cancel_count >= BG_CANCEL_COUNT)
                bg_error(6u);
        }
        return 1;
    }
    if (!bg_uart_binary_active()) return 0;
    bg_last_activity = bg_now();
    if (bg_state == BG_WAIT_PACKET || bg_state == BG_FRAME_WAIT_PACKET) {
        if (byte == BG_CAN) {
            if (++bg_cancel_count >= BG_CANCEL_COUNT) {
                bg_error(6u);
            }
            return 1;
        }
        bg_cancel_count = 0u;
        if (byte == BG_EOT) {
            if (bg_received_bytes != bg_expected_bytes) {
                bg_error(7u);
            } else if (bg_frame_mode) {
                if (bg_resources_blocked()) {
                    bg_error(10u);
                    return 1;
                }
                if (bg_ddr_adapter_finish(bg_received_bytes,
                                          bg_crc_state ^ 0xFFFFFFFFu) != 0 ||
                    (bg_crc_state ^ 0xFFFFFFFFu) != bg_expected_crc) {
                    bg_error(5u);
                } else {
                    uart_write(BSP_UART_TERMINAL, (char)BG_ACK);
                    bg_write_text("BG STORED "); bg_write_u32(bg_received_bytes);
                    bg_write_text(" "); bg_write_hex32(bg_crc_state ^ 0xFFFFFFFFu);
                    bg_write_text("\r\n");
                    bg_state = BG_FRAME_STORED;
                }
            } else {
                bg_checked();
            }
            return 1;
        }
        if (byte != BG_SOH) return 1;
        bg_packet_pos = 0u;
        bg_state = BG_PACKET;
        return 1;
    }
    if (bg_packet_pos == 0u) {
        bg_packet_seq = byte;
        bg_packet_pos = 1u;
    } else if (bg_packet_pos == 1u) {
        bg_packet_inverse = byte;
        bg_packet_pos = 2u;
    } else if (bg_packet_pos < 130u) {
        bg_packet[bg_packet_pos - 2u] = byte;
        ++bg_packet_pos;
    } else if (bg_packet_pos == 130u) {
        bg_packet_crc_hi = byte;
        ++bg_packet_pos;
    } else {
        crc_received = (uint16_t)(((uint16_t)bg_packet_crc_hi << 8) | byte);
        crc_calculated = bg_crc16(bg_packet, BG_BLOCK_SIZE);
        previous_seq = (uint8_t)(bg_expected_seq - 1u);
        if ((uint32_t)bg_packet_inverse !=
            ((255u - (uint32_t)bg_packet_seq) & 0xFFu) ||
            crc_received != crc_calculated) {
            uart_write(BSP_UART_TERMINAL, (char)BG_NAK);
            bg_state = bg_frame_mode ? BG_FRAME_WAIT_PACKET : BG_WAIT_PACKET;
            bg_packet_pos = 0u;
            return 1;
        }
        if (bg_packet_seq == bg_expected_seq) {
            if (bg_frame_mode && bg_resources_blocked()) {
                bg_error(10u);
                bg_packet_pos = 0u;
                return 1;
            }
            if (bg_frame_mode &&
                (bg_ddr_adapter_accept_block(bg_packet) != 0)) {
                bg_error(10u);
                bg_packet_pos = 0u;
                return 1;
            }
            bg_accept_packet();
        } else if (bg_have_previous && bg_packet_seq == previous_seq &&
                   memcmp(bg_packet, bg_previous, BG_BLOCK_SIZE) == 0) {
            uart_write(BSP_UART_TERMINAL, (char)BG_ACK);
            bg_state = bg_frame_mode ? BG_FRAME_WAIT_PACKET : BG_WAIT_PACKET;
        } else {
            bg_error(8u);
        }
        bg_packet_pos = 0u;
    }
    return 1;
}

void bg_uart_service_protocol_only(void)
{
    u64 now;
    if (bg_state == BG_PREPARING) {
        now = bg_now();
        if (bg_elapsed(now, bg_last_activity,
                       (u64)BSP_CLINT_HZ * BG_START_TIMEOUT_SEC))
            bg_error(9u);
        return;
    }
    if (!bg_uart_binary_active()) return;
    now = bg_now();
    if (bg_elapsed(now, bg_last_activity,
                   (u64)BSP_CLINT_HZ *
                   (bg_state == BG_PACKET ? BG_PACKET_TIMEOUT_SEC :
                                             BG_START_TIMEOUT_SEC)))
        bg_error(9u);
}

void bg_uart_service(void)
{
    int restore_status;
    int ready;
    if (bg_abort_pending && !bg_resources_blocked()) {
        if (!bg_ddr_adapter_active() || bg_ddr_adapter_abort() == 0)
            bg_abort_pending = 0u;
    }
    if ((bg_exit_phase == BG_EXIT_RESTORING) &&
        (bg_restore_status_hook != 0)) {
        restore_status = bg_restore_status_hook();
        if (restore_status == BG_RESTORE_STATUS_RESTORED) {
            bg_exit_phase = BG_EXIT_RESTORED;
            bg_write_text("BG RESTORED\r\n");
        } else if (restore_status == BG_RESTORE_STATUS_FAILED) {
            bg_exit_phase = BG_EXIT_FAILED;
            bg_write_text("BG ERROR RESTORE_FAILED\r\n");
        }
    }
    if (bg_resources_blocked()) {
        bg_uart_service_protocol_only();
        return;
    }
    if (bg_state != BG_PREPARING) {
        bg_uart_service_protocol_only();
        return;
    }
    ready = (bg_prepare_hook != 0) ? bg_prepare_hook() : -1;
    if (ready < 0) {
        bg_error(11u);
    } else if (ready > 0) {
        if (bg_ddr_adapter_begin(bg_saved_length, bg_saved_crc) != 0)
            bg_error(12u);
        else {
            bg_state = BG_FRAME_WAIT_PACKET;
            bg_last_activity = bg_now();
            bg_write_text("BG READY\r\n");
            uart_write(BSP_UART_TERMINAL, (char)BG_CRC_REQUEST);
        }
    } else {
        bg_uart_service_protocol_only();
    }
}
#endif
