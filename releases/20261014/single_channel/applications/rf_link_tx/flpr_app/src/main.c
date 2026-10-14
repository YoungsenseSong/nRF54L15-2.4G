#include <stdint.h>
#if defined(CONFIG_CACHE_MANAGEMENT)
#include <zephyr/cache.h>
#endif
#include <zephyr/kernel.h>
#include <zephyr/sys/barrier.h>
#include <zephyr/sys/crc.h>

#include "adc_sampler.h"
#include "ipc_tx.h"
#include "lp_trace.h"
#include "mems_batch.h"

#define BATCH_IPC_TIMEOUT_MS 20u

static void update_lp_trace(int last_ret)
{
	struct adc_sampler_stats sensor_stats;
	struct ipc_tx_stats ipc_stats;

	adc_sampler_stats_get(&sensor_stats);
	ipc_tx_stats_get(&ipc_stats);
	rf_link_lp_trace_note_sensor(sensor_stats.irq_count,
				 sensor_stats.poll_fallbacks,
				 sensor_stats.fifo_packets,
				 sensor_stats.samples_captured,
				 sensor_stats.malformed_packets,
				 sensor_stats.spi_errors,
				 sensor_stats.fifo_overflows);
	rf_link_lp_trace_note_ipc(ipc_stats.sent, ipc_stats.busy,
				  ipc_stats.failed, ipc_stats.released,
				  ipc_stats.bad_release, ipc_stats.slot_waits,
				  last_ret);
}

int main(void)
{
	uint32_t batch_seq = 0u;
	int ret;

	rf_link_lp_trace_boot();

	ret = ipc_tx_init();
	if (ret != 0) {
		rf_link_lp_trace_note_fatal((uint32_t)-ret);
		return ret;
	}
	rf_link_lp_trace_set_stage(RF_LINK_LP_STAGE_IPC_READY);

	ret = ipc_tx_wait_bound(K_FOREVER);
	if (ret != 0) {
		rf_link_lp_trace_note_fatal((uint32_t)-ret);
		return ret;
	}
	rf_link_lp_trace_set_stage(RF_LINK_LP_STAGE_IPC_BOUND);

	ret = adc_sampler_init();
	if (ret != 0) {
		update_lp_trace(ret);
		rf_link_lp_trace_note_fatal((uint32_t)-ret);
		return ret;
	}
	rf_link_lp_trace_set_stage(RF_LINK_LP_STAGE_SENSOR_READY);

	while (1) {
		volatile struct rf_link_mems_batch *shared_batch;
		int16_t *samples;
		uint32_t slot_index;
		uint32_t crc;

		ret = ipc_tx_acquire_slot(&slot_index, K_FOREVER);
		if (ret != 0) {
			update_lp_trace(ret);
			continue;
		}

		shared_batch = rf_link_mems_batch_slot(slot_index);
		if (shared_batch == NULL) {
			rf_link_lp_trace_note_fatal(0x100u);
			return -EINVAL;
		}

		shared_batch->magic = RF_LINK_MEMS_BATCH_MAGIC;
		shared_batch->version = RF_LINK_MEMS_BATCH_VERSION;
		shared_batch->slot_index = slot_index;
		shared_batch->batch_seq = batch_seq;
		shared_batch->sample_count = RF_LINK_MEMS_BATCH_SAMPLE_COUNT;
		shared_batch->capture_start_ms = k_uptime_get_32();
		rf_link_lp_trace_note_loop(batch_seq);

		samples = (int16_t *)(uintptr_t)&shared_batch->samples[0];
		ret = adc_sampler_read_samples(samples,
					       RF_LINK_MEMS_BATCH_SAMPLE_COUNT);
		if (ret != 0) {
			update_lp_trace(ret);
			rf_link_lp_trace_note_fatal((uint32_t)-ret);
			return ret;
		}

		shared_batch->capture_end_ms = k_uptime_get_32();
		crc = crc32_ieee((const uint8_t *)samples,
				 sizeof(shared_batch->samples));
		shared_batch->sample_crc32 = crc;
		barrier_dmem_fence_full();
#if defined(CONFIG_CACHE_MANAGEMENT)
		(void)sys_cache_data_flush_range((void *)(uintptr_t)shared_batch,
						 sizeof(*shared_batch));
#endif
		barrier_dmem_fence_full();

		ret = ipc_tx_publish_batch(slot_index, batch_seq, crc,
					   K_MSEC(BATCH_IPC_TIMEOUT_MS));
		update_lp_trace(ret);
		if (ret == 0) {
			rf_link_lp_trace_note_batch(batch_seq, slot_index);
			batch_seq++;
		}
	}
}
