#include "debug_uart.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>

#define DEBUG_UART_NODE DT_NODELABEL(uart30)

static const struct device *const uart_dev = DEVICE_DT_GET_OR_NULL(DEBUG_UART_NODE);
static bool ready;

bool debug_uart_init(void)
{
	ready = (uart_dev != NULL) && device_is_ready(uart_dev);
	return ready;
}

bool debug_uart_ready(void)
{
	return ready;
}

void debug_uart_putc(char c)
{
	if (!ready) {
		return;
	}

	uart_poll_out(uart_dev, (unsigned char)c);
}

void debug_uart_puts(const char *s)
{
	if (!ready || s == NULL) {
		return;
	}

	while (*s != '\0') {
		uart_poll_out(uart_dev, (unsigned char)*s);
		s++;
	}
}

void debug_uart_u32(uint32_t value)
{
	char buf[11];
	int pos = (int)sizeof(buf) - 1;

	buf[pos] = '\0';
	do {
		pos--;
		buf[pos] = (char)('0' + (value % 10u));
		value /= 10u;
	} while (value != 0u);

	debug_uart_puts(&buf[pos]);
}

void debug_uart_i32(int32_t value)
{
	if (value < 0) {
		debug_uart_putc('-');
		debug_uart_u32((uint32_t)(-value));
	} else {
		debug_uart_u32((uint32_t)value);
	}
}

void debug_uart_crlf(void)
{
	debug_uart_puts("\r\n");
}
