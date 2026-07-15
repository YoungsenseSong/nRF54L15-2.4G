#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <zephyr/cache.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/barrier.h>
#include <zephyr/sys/crc.h>

#include "debug_uart.h"
#include "ipc_bridge.h"
#include "lp_trace.h"
#include "mems_batch.h"
#include "radio_link.h"
#include "rf_link_proto.h"

#define BATCH_RELEASE_TIMEOUT_MS 20u
#define BATCH_RELEASE_RETRIES    3u

struct batch_tx_stats {
	uint32_t batches_received;
	uint32_t batches_sent;
	uint32_t batches_failed;
	uint32_t batch_bad_header;
	uint32_t batch_bad_crc;
	uint32_t frames_attempted;
	uint32_t frames_sent;
	uint32_t frames_failed;
	uint32_t samples_sent;
	uint32_t release_errors;
};

static struct batch_tx_stats batch_stats;
static uint16_t rf_seq;
static bool radio_ready;

static void print_boot_line(void)
{
	debug_uart_puts("\r\nrf_link_tx HP started");
	debug_uart_crlf();
	debug_uart_puts("mode=MEMS_BATCH_ESB_PTX,phy=");
	debug_uart_puts(radio_link_phy_label());
	debug_uart_puts(",ack=");
	debug_uart_puts(RF_LINK_NOACK_STREAM ? "noack" : "ack");
	debug_uart_puts(",channel=");
	debug_uart_u32(RF_LINK_CHANNEL);
	debug_uart_puts(",batch_samples=");
	debug_uart_u32(RF_LINK_MEMS_BATCH_SAMPLE_COUNT);
	debug_uart_puts(",samples_per_frame=");
	debug_uart_u32(RF_LINK_FRAME_SAMPLE_COUNT);
	debug_uart_puts(",sample_hz=");
	debug_uart_u32(RF_LINK_TARGET_SAMPLE_HZ);
	debug_uart_crlf();
}

static void print_status(void)
{
	struct ipc_bridge_stats ipc_stats;
	struct rf_link_lp_trace lp_trace;
	struct radio_link_stats radio_stats = {0};
	bool have_lp_trace;

	ipc_bridge_stats_get(&ipc_stats);
	if (radio_ready) {
		radio_link_stats_get(&radio_stats);
	}
	have_lp_trace = rf_link_lp_trace_snapshot(&lp_trace);

	debug_uart_puts("TX stat batches_rx=");
	debug_uart_u32(batch_stats.batches_received);
	debug_uart_puts(" batches_ok=");
	debug_uart_u32(batch_stats.batches_sent);
	debug_uart_puts(" batch_bad_hdr=");
	debug_uart_u32(batch_stats.batch_bad_header);
	debug_uart_puts(" batches_fail=");
	debug_uart_u32(batch_stats.batches_failed);
	debug_uart_puts(" batch_bad_crc=");
	debug_uart_u32(batch_stats.batch_bad_crc);
	debug_uart_puts(" frames_try=");
	debug_uart_u32(batch_stats.frames_attempted);
	debug_uart_puts(" frames_ok=");
	debug_uart_u32(batch_stats.frames_sent);
	debug_uart_puts(" frames_fail=");
	debug_uart_u32(batch_stats.frames_failed);
	debug_uart_puts(" samples_ok=");
	debug_uart_u32(batch_stats.samples_sent);
	debug_uart_puts(" ipc_bad=");
	debug_uart_u32(ipc_stats.bad_size + ipc_stats.bad_message);
	debug_uart_puts(" ipc_drop=");
	debug_uart_u32(ipc_stats.queue_drop);
	debug_uart_puts(" release_err=");
	debug_uart_u32(batch_stats.release_errors + ipc_stats.release_fail);
	debug_uart_puts(" rf_ok=");
	debug_uart_u32(radio_stats.tx_ok);
	debug_uart_puts(" rf_fail=");
	debug_uart_u32(radio_stats.tx_failed);
	debug_uart_puts(" rf_timeout=");
	debug_uart_u32(radio_stats.tx_timeout);
	debug_uart_puts(" mac_avg_us=");
	debug_uart_u32(radio_stats.mac_latency_avg_us);
	debug_uart_puts(" lp_stage=");
	debug_uart_u32(have_lp_trace ? lp_trace.stage : 0u);
	debug_uart_puts(" lp_batches=");
	debug_uart_u32(have_lp_trace ? lp_trace.batches_ready : 0u);
	debug_uart_puts(" lp_samples=");
	debug_uart_u32(have_lp_trace ? lp_trace.sensor_samples : 0u);
	debug_uart_puts(" lp_irq=");
	debug_uart_u32(have_lp_trace ? lp_trace.sensor_irq_count : 0u);
	debug_uart_puts(" lp_poll=");
	debug_uart_u32(have_lp_trace ? lp_trace.sensor_poll_fallbacks : 0u);
	debug_uart_puts(" lp_malformed=");
	debug_uart_u32(have_lp_trace ? lp_trace.sensor_malformed : 0u);
	debug_uart_puts(" lp_io_err=");
	debug_uart_u32(have_lp_trace ? lp_trace.sensor_io_errors : 0u);
	debug_uart_puts(" lp_fifo_ovf=");
	debug_uart_u32(have_lp_trace ? lp_trace.fifo_overflows : 0u);
	debug_uart_puts(" lp_slot_wait=");
	debug_uart_u32(have_lp_trace ? lp_trace.slot_waits : 0u);
	debug_uart_puts(" lp_fatal=");
	debug_uart_u32(have_lp_trace ? lp_trace.fatal_count : 0u);
	debug_uart_puts(" lp_fatal_reason=");
	debug_uart_u32(have_lp_trace ? lp_trace.fatal_reason : 0u);
	debug_uart_crlf();
}

static int ensure_radio_ready(void)
{
	int ret;

	if (radio_ready) {
		return 0;
	}

	ret = radio_link_init();
	if (ret == 0) {
		radio_ready = true;
		debug_uart_puts("radio ready after first MEMS batch");
		debug_uart_crlf();
	}

	return ret;
}

static bool batch_header_is_valid(
	const volatile struct rf_link_mems_batch *batch,
	const struct rf_link_ipc_message *message)
{
	return batch != NULL && message != NULL &&
	       batch->magic == RF_LINK_MEMS_BATCH_MAGIC &&
	       batch->version == RF_LINK_MEMS_BATCH_VERSION &&
	       batch->slot_index == message->slot_index &&
	       batch->batch_seq == message->batch_seq &&
	       batch->sample_count == RF_LINK_MEMS_BATCH_SAMPLE_COUNT &&
	       batch->sample_crc32 == message->sample_crc32;
}

static int transmit_batch(const struct rf_link_ipc_message *message)
{
	volatile struct rf_link_mems_batch *batch;
	const int16_t *samples;
	uint32_t crc;
	uint32_t offset = 0u;
	int first_error = 0;
	int ret;

	batch = rf_link_mems_batch_slot(message->slot_index);
	if (batch == NULL) {
		batch_stats.batch_bad_header++;
		return -EINVAL;
	}

	(void)sys_cache_data_invd_range((void *)(uintptr_t)batch, sizeof(*batch));
	barrier_dmem_fence_full();
	batch_stats.batches_received++;

	if (!batch_header_is_valid(batch, message)) {
		batch_stats.batch_bad_header++;
		return -EBADMSG;
	}

	samples = (const int16_t *)(uintptr_t)&batch->samples[0];
	crc = crc32_ieee((const uint8_t *)samples, sizeof(batch->samples));
	if (crc != message->sample_crc32) {
		batch_stats.batch_bad_crc++;
		return -EBADMSG;
	}

	ret = ensure_radio_ready();
	if (ret != 0) {
		return ret;
	}

	while (offset < RF_LINK_MEMS_BATCH_SAMPLE_COUNT) {
		struct rf_frame frame = {0};
		uint32_t remaining = RF_LINK_MEMS_BATCH_SAMPLE_COUNT - offset;
		uint32_t frame_samples = MIN(remaining, RF_LINK_FRAME_SAMPLE_COUNT);

		frame.magic = RF_LINK_MAGIC;
		frame.seq = rf_seq++;
		frame.sample_count = (uint16_t)frame_samples;
		frame.flags = RF_LINK_FRAME_FLAGS_MEMS;
		if (offset == 0u) {
			frame.flags |= RF_LINK_FRAME_FLAGS_BATCH_START;
		}
		if (frame_samples == remaining) {
			frame.flags |= RF_LINK_FRAME_FLAGS_BATCH_END;
		}
		frame.timestamp_ms = batch->capture_start_ms +
			(uint32_t)(((uint64_t)offset * 1000u) /
				   RF_LINK_TARGET_SAMPLE_HZ);
		memcpy(frame.samples, &samples[offset],
		       frame_samples * sizeof(frame.samples[0]));

		batch_stats.frames_attempted++;
		ret = radio_link_send_frame(&frame, K_MSEC(5));
		if (ret == 0) {
			batch_stats.frames_sent++;
			batch_stats.samples_sent += frame_samples;
		} else {
			batch_stats.frames_failed++;
			if (first_error == 0) {
				first_error = ret;
			}
		}
		offset += frame_samples;
	}

	if (first_error == 0) {
		batch_stats.batches_sent++;
	} else {
		batch_stats.batches_failed++;
	}

	return first_error;
}

static void release_batch(const struct rf_link_ipc_message *message)
{
	int ret = -EIO;

	for (uint32_t attempt = 0u; attempt < BATCH_RELEASE_RETRIES; attempt++) {
		ret = ipc_bridge_release_batch(message->slot_index, message->batch_seq,
					       K_MSEC(BATCH_RELEASE_TIMEOUT_MS));
		if (ret == 0) {
			return;
		}
	}

	batch_stats.release_errors++;
}

int main(void)
{
	struct rf_link_ipc_message message;
	uint32_t last_status_ms;
	int ret;

	(void)debug_uart_init();
	print_boot_line();
	memset(&batch_stats, 0, sizeof(batch_stats));
	rf_seq = 0u;
	radio_ready = false;

	ret = ipc_bridge_init();
	if (ret != 0) {
		debug_uart_puts("ipc init failed err=");
		debug_uart_i32(ret);
		debug_uart_crlf();
		return ret;
	}

	ret = ipc_bridge_wait_bound(K_FOREVER);
	if (ret != 0) {
		debug_uart_puts("ipc bind failed err=");
		debug_uart_i32(ret);
		debug_uart_crlf();
		return ret;
	}

	debug_uart_puts("LP IPC bound; CPUAPP blocking until a 4096-sample batch is ready");
	debug_uart_crlf();
	last_status_ms = k_uptime_get_32();

	while (1) {
		ret = ipc_bridge_wait_batch(&message, K_FOREVER);
		if (ret != 0) {
			continue;
		}

		ret = transmit_batch(&message);
		if (ret != 0) {
			debug_uart_puts("batch rejected/failed seq=");
			debug_uart_u32(message.batch_seq);
			debug_uart_puts(" err=");
			debug_uart_i32(ret);
			debug_uart_crlf();
		}
		release_batch(&message);

		if ((uint32_t)(k_uptime_get_32() - last_status_ms) >=
		    RF_LINK_STATUS_PERIOD_MS) {
			print_status();
			last_status_ms = k_uptime_get_32();
		}
	}
}
