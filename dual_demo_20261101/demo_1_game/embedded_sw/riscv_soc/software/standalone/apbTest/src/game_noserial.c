/* GHD game ELF (BUILD_VARIANT_GAME): the serial reply queue and the BG/UART upload paths do not exist. These definitions keep the
 * shared game code (enemy_test.c, game_lifecycle.c) unchanged in behaviour: text output is discarded, no BG/upload session is ever
 * active, the reply queue is always empty. Nothing here touches the UART or DDR. */
#ifdef BUILD_VARIANT_GAME
#include "uart_reply.h"
#include "bg_uart.h"
#include "bg_ddr_adapter.h"
int uart_reply_pending(void){return 0;}
int uart_reply_query_ready(void){return 1;}
int uart_reply_begin(unsigned limit){(void)limit;return 1;}
void uart_reply_commit(void){}
void uart_reply_service(void){}
void uart_reply_event(const char *text){(void)text;}
void uart_reply_write(uint32_t port,char value){(void)port;(void)value;}
void uart_reply_writeStr(uint32_t port,const char *text){(void)port;(void)text;}
void uart_reply_writeHex(uint32_t port,int value){(void)port;(void)value;}
unsigned uart_reply_queued(void){return 0;}
uint32_t uart_reply_sent_total(void){return 0;}
int bg_uart_binary_active(void){return 0;}
int bg_uart_recovery_active(void){return 0;}
int bg_ddr_adapter_active(void){return 0;}
#endif
