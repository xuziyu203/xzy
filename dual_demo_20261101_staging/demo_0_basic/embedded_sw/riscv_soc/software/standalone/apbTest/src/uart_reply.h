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
#endif
