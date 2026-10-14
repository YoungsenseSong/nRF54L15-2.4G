#ifndef RF_LINK_DEBUG_UART_H_
#define RF_LINK_DEBUG_UART_H_

#include <stdint.h>
#include <stdbool.h>

bool debug_uart_init(void);
bool debug_uart_ready(void);
void debug_uart_putc(char c);
void debug_uart_puts(const char *s);
void debug_uart_u32(uint32_t value);
void debug_uart_i32(int32_t value);
void debug_uart_crlf(void);

#endif /* RF_LINK_DEBUG_UART_H_ */
