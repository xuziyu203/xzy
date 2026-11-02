#ifndef UART_REPLY_H
#define UART_REPLY_H
#include <stdint.h>

#define UART_REPLY_CAPACITY 4096u
#define UART_REPLY_QUERY_MAX 1536u
#define UART_REPLY_EVENT_RESERVE 256u
#define UART_REPLY_SLICE 16u

int uart_reply_pending(void);
int uart_reply_query_ready(void);
int uart_reply_begin(unsigned limit);
void uart_reply_commit(void);
void uart_reply_service(void);
void uart_reply_event(const char *text);
/* Capture inside begin/commit; otherwise retain the synchronous protocol path. */
void uart_reply_write(uint32_t port, char value);
void uart_reply_writeStr(uint32_t port, const char *text);
void uart_reply_writeHex(uint32_t port, int value);
/* Stage-1 D: committed bytes not yet written to the UART, and a running count
 * of bytes written to the UART since reset (wraps). Observation only. */
unsigned uart_reply_queued(void);
uint32_t uart_reply_sent_total(void);
/* O2b: while holding, new replies/events go to a small side buffer that is
 * appended to the queue by hold_release(); direct(1) bypasses the hold for
 * the one deferred query that must be queued ahead of it. */
#define UART_REPLY_HOLD_CAPACITY 192u
#define UART_REPLY_HOLD_MIN 48u
void uart_reply_hold_enter(void);
int uart_reply_holding(void);
int uart_reply_hold_room(void);
unsigned uart_reply_hold_bytes(void);
int uart_reply_hold_release(void);
void uart_reply_direct(unsigned on);
#endif
