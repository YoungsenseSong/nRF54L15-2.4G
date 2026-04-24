#include <stdint.h>
#include <zephyr/kernel.h>

#include "debug_uart.h"
#include "proto.h"
#include "radio_link.h"
#include "rx_reorder.h"
#include "rx_sample_stream.h"

static void print_boot_line(void)
{
	debug_uart_puts("\r\nrf_link_rx started");
	debug_uart_crlf();
	debug_uart_puts("mode=ESB_PRX,phy=");
	debug_uart_puts(radio_link_phy_label());
	debug_uart_puts(",ack=");
	debug_uart_puts(RF_LINK_NOACK_STREAM ? "noack" : "ack");
	debug_uart_puts(",channel=");
	debug_uart_u32(RF_LINK_CHANNEL);
	debug_uart_puts(",samples_per_frame=");
	debug_uart_u32(RF_LINK_FRAME_SAMPLE_COUNT);
	debug_uart_crlf();
}

static void print_stats(uint32_t last_bytes, int64_t last_ms)
{
	struct radio_link_stats radio_stats;
	struct rx_reorder_stats rx_stats;
	uint32_t delta_bytes;
	uint32_t bps = 0;
	int64_t now = k_uptime_get();
	int64_t delta_ms = now - last_ms;

	radio_link_stats_get(&radio_stats);
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
	debug_uart_puts(" rf_evt=");
	debug_uart_u32(radio_stats.rx_events);
	debug_uart_puts(" rf_frames=");
	debug_uart_u32(radio_stats.rx_frames);
	debug_uart_puts(" rf_read_err=");
	debug_uart_u32(radio_stats.rx_read_errors);
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
	bool stream_mode = false;
	int ret;

	(void)debug_uart_init();
	print_boot_line();

#if defined(CONFIG_RF_LINK_RX_SAMPLE_STREAM)
	if (debug_uart_ready()) {
		debug_uart_puts("rx_sample_stream enabled, switch host to baud=");
		debug_uart_u32(CONFIG_RF_LINK_RX_SAMPLE_STREAM_UART_BAUD);
		debug_uart_crlf();
	}
#endif

	if (IS_ENABLED(CONFIG_RF_LINK_RX_SAMPLE_STREAM)) {
		ret = rx_sample_stream_init();
		if (ret != 0) {
			if (debug_uart_ready()) {
				debug_uart_puts("rx_sample_stream init failed err=");
				debug_uart_i32(ret);
				debug_uart_crlf();
				debug_uart_puts("fall back to RX stat mode");
				debug_uart_crlf();
			}
		} else {
			stream_mode = true;
		}
	}

	rx_reorder_init();

	ret = radio_link_init();
	if (ret != 0) {
		if (!stream_mode) {
			debug_uart_puts("radio init failed err=");
			debug_uart_i32(ret);
			debug_uart_crlf();
		}
		return ret;
	}

	if (!stream_mode) {
		debug_uart_puts("radio ready, waiting packets");
		debug_uart_crlf();
	}

	last_ms = k_uptime_get();
	while (1) {
		k_sleep(K_MSEC(RF_LINK_STATUS_PERIOD_MS));
		if (!stream_mode) {
			print_stats(last_bytes, last_ms);
			rx_reorder_stats_get(&rx_stats);
			last_bytes = rx_stats.bytes;
			last_ms = k_uptime_get();
		}
	}
}
