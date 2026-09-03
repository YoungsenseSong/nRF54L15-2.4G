#include <stdint.h>
#include <string.h>
#include <zephyr/app_version.h>
#include <zephyr/kernel.h>

#include "debug_uart.h"
#include "rf_link_proto.h"
#include "radio_link.h"
#include "rx_reorder.h"
#include "rx_sample_stream.h"
#include "timebase.h"

#if defined(CONFIG_RF_LINK_FUTURE_MODE)
#include "fpga_transport.h"
#include "frame_queue.h"
#include "link_control.h"
#include "sync_manager.h"
#endif

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
	debug_uart_puts(",pipe=");
	debug_uart_u32(RF_LINK_PIPE);
	debug_uart_puts(",prefix=0x54,fw=");
	debug_uart_puts(STRINGIFY(APP_BUILD_VERSION));
	debug_uart_puts(",samples_per_frame=");
	debug_uart_u32(RF_LINK_FRAME_SAMPLE_COUNT);
#if defined(CONFIG_RF_LINK_FUTURE_MODE)
	debug_uart_puts(",v2_preview=1,node_id=");
	debug_uart_u32(CONFIG_RF_LINK_NODE_ID);
#endif
	debug_uart_crlf();
}

#if !defined(CONFIG_RF_LINK_FUTURE_MODE)
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
	debug_uart_puts(" late=");
	debug_uart_u32(rx_stats.late_frames);
	debug_uart_puts(" bad=");
	debug_uart_u32(rx_stats.bad_magic + rx_stats.bad_size +
		       rx_stats.bad_sample_count);
	debug_uart_puts(" rf_evt=");
	debug_uart_u32(radio_stats.rx_events);
	debug_uart_puts(" rf_frames=");
	debug_uart_u32(radio_stats.rx_frames);
	debug_uart_puts(" rf_read_err=");
	debug_uart_u32(radio_stats.rx_read_errors);
	debug_uart_puts(" rf_q_ovf=");
	debug_uart_u32(radio_stats.rx_queue_overflow);
	debug_uart_puts(" rf_q_hwm=");
	debug_uart_u32(radio_stats.rx_queue_high_water);
	debug_uart_puts(" seq=");
	debug_uart_u32(rx_stats.last_seq);
	debug_uart_puts(" first=");
	debug_uart_i32(rx_stats.last_first_sample);
	debug_uart_puts(" last=");
	debug_uart_i32(rx_stats.last_last_sample);
	debug_uart_crlf();
}
#endif

#if defined(CONFIG_RF_LINK_FUTURE_MODE)
static void print_future_stats(void)
{
	struct frame_queue_stats queue_stats;
	struct link_control_status control_stats;
	struct radio_link_stats radio_stats;
	struct rx_reorder_stats reorder_stats;
	struct sync_status sync_stats;
	struct transport_stats transport_stats;

	radio_link_stats_get(&radio_stats);
	rx_reorder_stats_get(&reorder_stats);
	frame_queue_stats_get(&queue_stats);
	sync_manager_get_status(&sync_stats);
	memset(&transport_stats, 0, sizeof(transport_stats));
	if (IS_ENABLED(CONFIG_RF_LINK_FPGA_TRANSPORT)) {
		fpga_transport_get_stats(&transport_stats);
	}
	link_control_get_status(&control_stats);

	debug_uart_puts("RADIO events=");
	debug_uart_u32(radio_stats.rx_events);
	debug_uart_puts(" frames=");
	debug_uart_u32(radio_stats.rx_frames);
	debug_uart_puts(" read_errors=");
	debug_uart_u32(radio_stats.rx_read_errors);
	debug_uart_puts(" queue_overflow=");
	debug_uart_u32(radio_stats.rx_queue_overflow);
	debug_uart_puts(" queue_high_water=");
	debug_uart_u32(radio_stats.rx_queue_high_water);
	debug_uart_crlf();

	debug_uart_puts("REORDER extended_frame_seq=");
	debug_uart_u32(reorder_stats.extended_frame_seq);
	debug_uart_puts(" rf_lost_frames=");
	debug_uart_u32(reorder_stats.lost_frames);
	debug_uart_puts(" rf_lost_samples=");
	debug_uart_u32(reorder_stats.lost_samples);
	debug_uart_puts(" rf_duplicates=");
	debug_uart_u32(reorder_stats.duplicates);
	debug_uart_puts(" rf_late_frames=");
	debug_uart_u32(reorder_stats.late_frames);
	debug_uart_puts(" index_estimated=");
	debug_uart_u32(reorder_stats.index_estimated);
	debug_uart_puts(" bad_magic=");
	debug_uart_u32(reorder_stats.bad_magic);
	debug_uart_puts(" bad_size=");
	debug_uart_u32(reorder_stats.bad_size);
	debug_uart_puts(" bad_sample_count=");
	debug_uart_u32(reorder_stats.bad_sample_count);
	debug_uart_crlf();

	debug_uart_puts("SYNC state=");
	debug_uart_u32(sync_stats.state);
	debug_uart_puts(" sync_capture_count=");
	debug_uart_u32(sync_stats.sync_capture_count);
	debug_uart_puts(" sync_timeout=");
	debug_uart_u32(sync_stats.sync_timeout);
	debug_uart_puts(" sync_epoch=");
	debug_uart_u32(sync_stats.sync_epoch);
	debug_uart_puts(" sync_locked=");
	debug_uart_u32(sync_stats.sync_locked ? 1u : 0u);
	debug_uart_puts(" resync_count=");
	debug_uart_u32(sync_stats.resync_count);
	debug_uart_crlf();

	debug_uart_puts("QUEUE push_total=");
	debug_uart_u32(queue_stats.push_total);
	debug_uart_puts(" pop_total=");
	debug_uart_u32(queue_stats.pop_total);
	debug_uart_puts(" overflow=");
	debug_uart_u32(queue_stats.overflow);
	debug_uart_puts(" high_water=");
	debug_uart_u32(queue_stats.high_water);
	debug_uart_puts(" level=");
	debug_uart_u32(frame_queue_level());
	debug_uart_crlf();

	debug_uart_puts("SPI_TRANSPORT records=");
	debug_uart_u32(transport_stats.records);
	debug_uart_puts(" crc_errors=");
	debug_uart_u32(transport_stats.crc_errors);
	debug_uart_puts(" invalid_cmd=");
	debug_uart_u32(transport_stats.invalid_cmd);
	debug_uart_puts(" duplicate_commit=");
	debug_uart_u32(transport_stats.duplicate_commit);
	debug_uart_puts(" submit_errors=");
	debug_uart_u32(transport_stats.submit_errors);
	debug_uart_puts(" stall_ms=");
	debug_uart_u32(transport_stats.stall_ms);
	debug_uart_puts(" req_xfer=");
	debug_uart_u32(transport_stats.request_transactions);
	debug_uart_puts(" rsp_xfer=");
	debug_uart_u32(transport_stats.response_transactions);
	debug_uart_puts(" spi_errors=");
	debug_uart_u32(transport_stats.spi_errors);
	debug_uart_puts(" parser_errors=");
	debug_uart_u32(transport_stats.parser_errors);
	debug_uart_puts(" short_xfer=");
	debug_uart_u32(transport_stats.short_transfers);
	debug_uart_crlf();

	debug_uart_puts("CONTROL armed=");
	debug_uart_u32(control_stats.arm_commands);
	debug_uart_puts(" started=");
	debug_uart_u32(control_stats.start_commands);
	debug_uart_puts(" stopped=");
	debug_uart_u32(control_stats.stop_commands);
	debug_uart_puts(" reset=");
	debug_uart_u32(control_stats.reset_commands);
	debug_uart_puts(" invalid=");
	debug_uart_u32(control_stats.invalid_commands);
	debug_uart_crlf();
}

static int future_pipeline_init(void)
{
	int ret;

	frame_queue_init();
	link_control_init();
	ret = sync_manager_init();
	if (ret != 0) {
		return ret;
	}

	if (IS_ENABLED(CONFIG_RF_LINK_FPGA_TRANSPORT)) {
		return fpga_transport_init();
	}

	return 0;
}

static void future_process_record(struct rx_frame_record *record)
{
	struct rx_reorder_stats reorder_stats;
	int ret;

	if ((record->status_flags & RX_FRAME_STATUS_GAP_BEFORE) != 0u) {
		rx_reorder_stats_get(&reorder_stats);
		sync_manager_on_sequence_gap(
			reorder_stats.last_missing.frame_count);
	}

	sync_manager_on_frame(record);
	if (!link_control_streaming()) {
		return;
	}

	ret = frame_queue_push(record);
	if (ret != 0) {
		sync_manager_on_queue_overflow();
		return;
	}

	if (IS_ENABLED(CONFIG_RF_LINK_FPGA_TRANSPORT)) {
		(void)fpga_transport_service();
	} else {
		(void)frame_queue_commit(record->extended_frame_seq);
	}
}
#endif

int main(void)
{
#if !defined(CONFIG_RF_LINK_FUTURE_MODE)
	struct rx_reorder_stats rx_stats;
	uint32_t last_bytes = 0;
#endif
	struct radio_link_rx_packet packet;
	struct rx_frame_record record;
	int64_t last_ms;
	bool stream_mode = false;
	int ret;

	(void)debug_uart_init();
	print_boot_line();
	ret = timebase_init();
	if (ret != 0) {
		debug_uart_puts("timebase init failed err=");
		debug_uart_i32(ret);
		debug_uart_crlf();
		return ret;
	}

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

#if defined(CONFIG_RF_LINK_FUTURE_MODE)
	ret = future_pipeline_init();
	if (ret != 0) {
		debug_uart_puts("future pipeline init failed err=");
		debug_uart_i32(ret);
		debug_uart_crlf();
		if (IS_ENABLED(CONFIG_RF_LINK_FPGA_SPIS)) {
			debug_uart_puts("CH0 SPIS backend init failed; check reviewed overlay and wiring");
			debug_uart_crlf();
		}
		return ret;
	}
#endif

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
		ret = radio_link_receive(&packet, K_MSEC(20));
		if (ret == 0) {
			ret = rx_reorder_process_packet(&packet.frame, packet.length,
						 packet.rx_tick, &record);
			if (ret == 0) {
#if defined(CONFIG_RF_LINK_FUTURE_MODE)
				future_process_record(&record);
#else
				rx_sample_stream_submit_frame(&record.frame);
#endif
			}
		}

#if defined(CONFIG_RF_LINK_FUTURE_MODE)
		sync_manager_poll(timebase_now_ticks());
		if (IS_ENABLED(CONFIG_RF_LINK_FPGA_TRANSPORT)) {
			(void)fpga_transport_service();
		}
#endif

		if (!stream_mode &&
		    (k_uptime_get() - last_ms) >= RF_LINK_STATUS_PERIOD_MS) {
#if defined(CONFIG_RF_LINK_FUTURE_MODE)
			print_future_stats();
#else
			print_stats(last_bytes, last_ms);
			rx_reorder_stats_get(&rx_stats);
			last_bytes = rx_stats.bytes;
#endif
			last_ms = k_uptime_get();
		}
	}
}
