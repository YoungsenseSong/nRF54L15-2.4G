#include <errno.h>
#include <stdint.h>
#include <zephyr/kernel.h>

#include "debug_uart.h"
#include "ipc_bridge.h"
#include "lp_trace.h"
#include "proto.h"
#include "radio_link.h"
#include "tx_queue.h"

static void print_boot_line(void)
{
	debug_uart_puts("\r\nrf_link_tx HP started");
	debug_uart_crlf();
	debug_uart_puts("mode=ESB_PTX,phy=");
	debug_uart_puts(radio_link_phy_label());
	debug_uart_puts(",ack=");
	debug_uart_puts(RF_LINK_NOACK_STREAM ? "noack" : "ack");
	debug_uart_puts(",channel=");
	debug_uart_u32(RF_LINK_CHANNEL);
	debug_uart_puts(",samples_per_frame=");
	debug_uart_u32(RF_LINK_FRAME_SAMPLE_COUNT);
	debug_uart_puts(",period_us=");
	debug_uart_u32(RF_LINK_TX_PERIOD_US);
	debug_uart_puts(",lp_pace=");
	debug_uart_puts(RF_LINK_LP_USE_ABSOLUTE_PACING ? "deadline" : "relative");
	debug_uart_crlf();
}

static void print_status(uint32_t sent_frames)
{
	struct ipc_bridge_stats ipc_stats;
	struct rf_link_lp_trace lp_trace;
	struct tx_queue_stats queue_stats;
	struct radio_link_stats radio_stats;
	bool have_lp_trace;

	ipc_bridge_stats_get(&ipc_stats);
	tx_queue_stats_get(&queue_stats);
	radio_link_stats_get(&radio_stats);
	have_lp_trace = rf_link_lp_trace_snapshot(&lp_trace);

	debug_uart_puts("TX stat sent=");
	debug_uart_u32(sent_frames);
	debug_uart_puts(" ipc_rx=");
	debug_uart_u32(ipc_stats.received);
	debug_uart_puts(" queued=");
	debug_uart_u32(ipc_stats.queued);
	debug_uart_puts(" q_drop=");
	debug_uart_u32(queue_stats.dropped + ipc_stats.queue_drop);
	debug_uart_puts(" ipc_bad_size=");
	debug_uart_u32(ipc_stats.bad_size);
	debug_uart_puts(" ipc_bad_magic=");
	debug_uart_u32(ipc_stats.bad_magic);
	debug_uart_puts(" rf_ok=");
	debug_uart_u32(radio_stats.tx_ok);
	debug_uart_puts(" rf_fail=");
	debug_uart_u32(radio_stats.tx_failed);
	debug_uart_puts(" rf_timeout=");
	debug_uart_u32(radio_stats.tx_timeout);
	debug_uart_puts(" rf_err=");
	debug_uart_u32(radio_stats.tx_errors);
	debug_uart_puts(" attempts=");
	debug_uart_u32(radio_stats.last_attempts);
	debug_uart_puts(" mac_cnt=");
	debug_uart_u32(radio_stats.mac_latency_count);
	debug_uart_puts(" mac_last_us=");
	debug_uart_u32(radio_stats.mac_latency_last_us);
	debug_uart_puts(" mac_min_us=");
	debug_uart_u32(radio_stats.mac_latency_min_us);
	debug_uart_puts(" mac_avg_us=");
	debug_uart_u32(radio_stats.mac_latency_avg_us);
	debug_uart_puts(" mac_max_us=");
	debug_uart_u32(radio_stats.mac_latency_max_us);
	debug_uart_puts(" lp_stage=");
	debug_uart_u32(have_lp_trace ? lp_trace.stage : 0u);
	debug_uart_puts(" lp_boots=");
	debug_uart_u32(have_lp_trace ? lp_trace.boot_count : 0u);
	debug_uart_puts(" lp_fatal=");
	debug_uart_u32(have_lp_trace ? lp_trace.fatal_count : 0u);
	debug_uart_puts(" lp_fatal_reason=");
	debug_uart_u32(have_lp_trace ? lp_trace.fatal_reason : 0u);
	debug_uart_puts(" lp_loop=");
	debug_uart_u32(have_lp_trace ? lp_trace.loop_count : 0u);
	debug_uart_puts(" lp_seq=");
	debug_uart_u32(have_lp_trace ? lp_trace.last_seq : 0u);
	debug_uart_puts(" lp_ok=");
	debug_uart_u32(have_lp_trace ? lp_trace.ipc_sent : 0u);
	debug_uart_puts(" lp_busy=");
	debug_uart_u32(have_lp_trace ? lp_trace.ipc_busy : 0u);
	debug_uart_puts(" lp_fail=");
	debug_uart_u32(have_lp_trace ? lp_trace.ipc_failed : 0u);
	debug_uart_puts(" lp_ret=");
	debug_uart_i32(have_lp_trace ? lp_trace.last_send_ret : 0);
	debug_uart_crlf();
}

static void maybe_print_status(uint32_t sent_frames, uint32_t *last_status_ms)
{
	uint32_t now_ms = k_uptime_get_32();

	if ((uint32_t)(now_ms - *last_status_ms) >= RF_LINK_STATUS_PERIOD_MS) {
		print_status(sent_frames);
		*last_status_ms = now_ms;
	}
}

int main(void)
{
	struct rf_frame frame;
	uint32_t sent_frames = 0;
	uint32_t last_status_ms;
	int ret;

	(void)debug_uart_init();
	print_boot_line();

	tx_queue_init();

	ret = radio_link_init();
	if (ret != 0) {
		debug_uart_puts("radio init failed err=");
		debug_uart_i32(ret);
		debug_uart_crlf();
		return ret;
	}
	debug_uart_puts("radio ready");
	debug_uart_crlf();

	debug_uart_puts("ipc init begin");
	debug_uart_crlf();
	ret = ipc_bridge_init();
	if (ret != 0) {
		debug_uart_puts("ipc init failed err=");
		debug_uart_i32(ret);
		debug_uart_crlf();
		return ret;
	}
	debug_uart_puts("ipc init ok");
	debug_uart_crlf();

	debug_uart_puts("waiting for LP IPC endpoint");
	debug_uart_crlf();
	ret = ipc_bridge_wait_bound(K_FOREVER);
	if (ret != 0) {
		debug_uart_puts("ipc bind failed err=");
		debug_uart_i32(ret);
		debug_uart_crlf();
		return ret;
	}

	debug_uart_puts("LP IPC endpoint bound");
	debug_uart_crlf();
	last_status_ms = k_uptime_get_32();

	while (1) {
		ret = tx_queue_get(&frame, K_SECONDS(1));
		if (ret == -EAGAIN) {
			maybe_print_status(sent_frames, &last_status_ms);
			continue;
		}
		if (ret != 0) {
			continue;
		}

		ret = radio_link_send_frame(&frame, K_MSEC(5));
		if (ret == 0) {
			sent_frames++;
		}

		maybe_print_status(sent_frames, &last_status_ms);
	}
}
