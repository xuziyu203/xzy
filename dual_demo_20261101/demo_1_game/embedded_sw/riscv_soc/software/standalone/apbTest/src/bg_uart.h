#ifndef APBTEST_BG_UART_H
#define APBTEST_BG_UART_H

#include <stdint.h>

/* Status returned by the main-loop restore observer.  EXIT acceptance is a
 * protocol event; RESTORED is reported only after the requested frame has
 * completed and the display mode/source match the saved target. */
#define BG_RESTORE_STATUS_NONE       0
#define BG_RESTORE_STATUS_RESTORING  1
#define BG_RESTORE_STATUS_RESTORED   2
#define BG_RESTORE_STATUS_FAILED     3

/* CHECK-only BG/1 receiver.  The physical UART read loop remains in main.c. */
void bg_uart_init(void);
void bg_uart_set_hooks(int (*prepare_hook)(void), int (*restore_hook)(void));
void bg_uart_set_restore_status_hook(int (*status_hook)(void));
/* Query the existing saved demo target, including after an upload abort. */
void bg_uart_set_restore_pending_hook(int (*pending_hook)(void));
int bg_uart_handle_line(const char *line);
int bg_uart_binary_active(void);
int bg_uart_preparing(void);
int bg_uart_recovery_active(void);
int bg_uart_replay_active(void);
int bg_uart_read_byte(uint8_t *byte);
/* Exclusive loan to ASSET, only at a completed/idle protocol boundary. */
void bg_uart_service_during_write(void);
int bg_uart_take_cancel(void);
int bg_uart_consume_byte(uint8_t byte);
void bg_uart_service(void);
/* Wait callbacks may service protocol bytes and deadlines, but must not run
 * the display-resource preparation hook. */
void bg_uart_service_protocol_only(void);
/* Block adapter/restore/abort side effects while a renderer wait callback is
 * active. The parser and timeout bookkeeping remain available. */
void bg_uart_set_resource_guard(int blocked);

#endif
