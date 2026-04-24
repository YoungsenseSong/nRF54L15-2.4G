#include <zephyr/fatal.h>

#include "debug_uart.h"

void k_sys_fatal_error_handler(unsigned int reason, const struct arch_esf *esf)
{
	ARG_UNUSED(esf);

	(void)debug_uart_init();
	debug_uart_puts("\r\nHP fatal reason=");
	debug_uart_u32(reason);
	debug_uart_crlf();

	k_fatal_halt(reason);
}
