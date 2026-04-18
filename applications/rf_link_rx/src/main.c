#include <stdint.h>
#include <zephyr/kernel.h>

#include "debug_uart.h"
#include "proto.h"
#include "radio_link.h"
#include "rx_reorder.h"

static void print_boot_line(void)
{
	debug_uart_puts("\r\nrf_link_rx started");
	debug_uart_crlf();
	debug_uart_puts("mode=ESB_PRX,phy=1M,channel=");
	debug_uart_u32(RF_LINK_CHANNEL);
	debug_uart_crlf();
}

static void print_stats(uint32_t last_bytes, int64_t last_ms)
{
	struct rx_reorder_stats rx_stats;
	uint32_t delta_bytes;
	uint32_t bps = 0;
	int64_t now = k_uptime_get();
	int64_t delta_ms = now - last_ms;

	rx_reorder_stats_get(&rx_stats);

	delta_bytes = rx_stats.bytes - last_bytes;
	if (delta_ms > 0) {
		bps = (uint32_t)(((uint64_t)delta_bytes * 8000ull) / (uint64_t)delta_ms);
	}

	debug_uart_puts("RX stat frames=");
	debug_uart_u32(rx_stats.frames);
	debug_uart_puts(" samples=");
	debug_uart_u32(rx_stats.samples);
	debug_uart_puts(" bps=");
	debug_uart_u32(bps);
	debug_uart_puts(" lost=");
	debug_uart_u32(rx_stats.lost_frames);
	debug_uart_puts(" dup=");
	debug_uart_u32(rx_stats.duplicates);
	debug_uart_puts(" bad=");
	debug_uart_u32(rx_stats.bad_magic + rx_stats.bad_size);
	debug_uart_puts(" seq=");
	debug_uart_u32(rx_stats.last_seq);
	debug_uart_puts(" first=");
	debug_uart_i32(rx_stats.last_first_sample);
	debug_uart_puts(" last=");
	debug_uart_i32(rx_stats.last_last_sample);
	debug_uart_crlf();
}

int main(void)
{
	struct rx_reorder_stats rx_stats;
	uint32_t last_bytes = 0;
	int64_t last_ms;
	int ret;

	(void)debug_uart_init();
	print_boot_line();

	rx_reorder_init();

	ret = radio_link_init();
	if (ret != 0) {
		debug_uart_puts("radio init failed err=");
		debug_uart_i32(ret);
		debug_uart_crlf();
		return ret;
	}

	debug_uart_puts("radio ready, waiting packets");
	debug_uart_crlf();

	last_ms = k_uptime_get();
	while (1) {
		k_sleep(K_SECONDS(1));
		print_stats(last_bytes, last_ms);
		rx_reorder_stats_get(&rx_stats);
		last_bytes = rx_stats.bytes;
		last_ms = k_uptime_get();
	}
}
