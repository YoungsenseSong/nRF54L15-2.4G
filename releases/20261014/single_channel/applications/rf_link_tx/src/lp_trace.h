#ifndef RF_LINK_LP_TRACE_H_
#define RF_LINK_LP_TRACE_H_

#include <stdbool.h>
#include <stdint.h>
#if defined(CONFIG_CACHE_MANAGEMENT)
#include <zephyr/cache.h>
#endif
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/barrier.h>

#define RF_LINK_LP_TRACE_MAGIC   0x4c505452u
#define RF_LINK_LP_TRACE_VERSION 2u

#define RF_LINK_LP_TRACE_NODE DT_NODELABEL(rf_link_lp_trace_mem)

#if !DT_NODE_HAS_STATUS(RF_LINK_LP_TRACE_NODE, okay)
#error "rf_link_lp_trace_mem is not enabled in devicetree"
#endif

enum rf_link_lp_stage {
	RF_LINK_LP_STAGE_RESET = 0u,
	RF_LINK_LP_STAGE_BOOT = 1u,
	RF_LINK_LP_STAGE_IPC_READY = 2u,
	RF_LINK_LP_STAGE_IPC_BOUND = 3u,
	RF_LINK_LP_STAGE_SENSOR_READY = 4u,
	RF_LINK_LP_STAGE_FILLING_BATCH = 5u,
	RF_LINK_LP_STAGE_BATCH_READY = 6u,
};

struct rf_link_lp_trace {
	uint32_t magic;
	uint32_t version;
	uint32_t boot_count;
	uint32_t fatal_count;
	uint32_t stage;
	uint32_t loop_count;
	uint32_t last_seq;
	uint32_t last_uptime_ms;
	uint32_t fatal_reason;
	int32_t last_send_ret;
	uint32_t ipc_sent;
	uint32_t ipc_busy;
	uint32_t ipc_failed;
	uint32_t ipc_released;
	uint32_t ipc_bad_release;
	uint32_t sensor_irq_count;
	uint32_t sensor_poll_fallbacks;
	uint32_t sensor_fifo_packets;
	uint32_t sensor_samples;
	uint32_t sensor_malformed;
	uint32_t sensor_io_errors;
	uint32_t batches_ready;
	uint32_t last_slot;
	uint32_t fifo_overflows;
	uint32_t slot_waits;
};

static inline volatile struct rf_link_lp_trace *rf_link_lp_trace_ptr(void)
{
	return (volatile struct rf_link_lp_trace *)(uintptr_t)
		DT_REG_ADDR(RF_LINK_LP_TRACE_NODE);
}

static inline void rf_link_lp_trace_boot(void)
{
	volatile struct rf_link_lp_trace *trace = rf_link_lp_trace_ptr();
	uint32_t boot_count = 1u;

	if (trace->magic == RF_LINK_LP_TRACE_MAGIC &&
	    trace->version == RF_LINK_LP_TRACE_VERSION) {
		boot_count = trace->boot_count + 1u;
	}

	trace->magic = RF_LINK_LP_TRACE_MAGIC;
	trace->version = RF_LINK_LP_TRACE_VERSION;
	trace->boot_count = boot_count;
	if (boot_count == 1u) {
		trace->fatal_count = 0u;
	}
	trace->stage = RF_LINK_LP_STAGE_BOOT;
	trace->loop_count = 0u;
	trace->last_seq = 0u;
	trace->last_uptime_ms = k_uptime_get_32();
	trace->fatal_reason = 0u;
	trace->last_send_ret = 0;
	trace->ipc_sent = 0u;
	trace->ipc_busy = 0u;
	trace->ipc_failed = 0u;
	trace->ipc_released = 0u;
	trace->ipc_bad_release = 0u;
	trace->sensor_irq_count = 0u;
	trace->sensor_poll_fallbacks = 0u;
	trace->sensor_fifo_packets = 0u;
	trace->sensor_samples = 0u;
	trace->sensor_malformed = 0u;
	trace->sensor_io_errors = 0u;
	trace->batches_ready = 0u;
	trace->last_slot = 0u;
	trace->fifo_overflows = 0u;
	trace->slot_waits = 0u;
	barrier_dmem_fence_full();
}

static inline void rf_link_lp_trace_set_stage(uint32_t stage)
{
	volatile struct rf_link_lp_trace *trace = rf_link_lp_trace_ptr();

	trace->stage = stage;
	trace->last_uptime_ms = k_uptime_get_32();
}

static inline void rf_link_lp_trace_note_loop(uint32_t seq)
{
	volatile struct rf_link_lp_trace *trace = rf_link_lp_trace_ptr();

	trace->stage = RF_LINK_LP_STAGE_FILLING_BATCH;
	trace->loop_count++;
	trace->last_seq = seq;
	trace->last_uptime_ms = k_uptime_get_32();
}

static inline void rf_link_lp_trace_note_ipc(uint32_t sent, uint32_t busy,
					     uint32_t failed, uint32_t released,
					     uint32_t bad_release, uint32_t slot_waits,
					     int32_t last_ret)
{
	volatile struct rf_link_lp_trace *trace = rf_link_lp_trace_ptr();

	trace->ipc_sent = sent;
	trace->ipc_busy = busy;
	trace->ipc_failed = failed;
	trace->ipc_released = released;
	trace->ipc_bad_release = bad_release;
	trace->slot_waits = slot_waits;
	trace->last_send_ret = last_ret;
	trace->last_uptime_ms = k_uptime_get_32();
}

static inline void rf_link_lp_trace_note_sensor(uint32_t irq_count,
						uint32_t poll_fallbacks,
						uint32_t fifo_packets,
						uint32_t samples,
						uint32_t malformed,
						uint32_t io_errors,
						uint32_t fifo_overflows)
{
	volatile struct rf_link_lp_trace *trace = rf_link_lp_trace_ptr();

	trace->sensor_irq_count = irq_count;
	trace->sensor_poll_fallbacks = poll_fallbacks;
	trace->sensor_fifo_packets = fifo_packets;
	trace->sensor_samples = samples;
	trace->sensor_malformed = malformed;
	trace->sensor_io_errors = io_errors;
	trace->fifo_overflows = fifo_overflows;
	trace->last_uptime_ms = k_uptime_get_32();
}

static inline void rf_link_lp_trace_note_batch(uint32_t batch_seq,
					       uint32_t slot_index)
{
	volatile struct rf_link_lp_trace *trace = rf_link_lp_trace_ptr();

	trace->stage = RF_LINK_LP_STAGE_BATCH_READY;
	trace->last_seq = batch_seq;
	trace->last_slot = slot_index;
	trace->batches_ready++;
	trace->last_uptime_ms = k_uptime_get_32();
	barrier_dmem_fence_full();
}

static inline void rf_link_lp_trace_note_fatal(uint32_t reason)
{
	volatile struct rf_link_lp_trace *trace = rf_link_lp_trace_ptr();

	trace->fatal_count++;
	trace->fatal_reason = reason;
	trace->last_uptime_ms = k_uptime_get_32();
}

static inline bool rf_link_lp_trace_snapshot(struct rf_link_lp_trace *out)
{
	volatile struct rf_link_lp_trace *trace = rf_link_lp_trace_ptr();

	if (out == NULL) {
		return false;
	}

#if defined(CONFIG_CACHE_MANAGEMENT)
	(void)sys_cache_data_invd_range((void *)(uintptr_t)trace, sizeof(*out));
#endif
	barrier_dmem_fence_full();

	out->magic = trace->magic;
	out->version = trace->version;
	out->boot_count = trace->boot_count;
	out->fatal_count = trace->fatal_count;
	out->stage = trace->stage;
	out->loop_count = trace->loop_count;
	out->last_seq = trace->last_seq;
	out->last_uptime_ms = trace->last_uptime_ms;
	out->fatal_reason = trace->fatal_reason;
	out->last_send_ret = trace->last_send_ret;
	out->ipc_sent = trace->ipc_sent;
	out->ipc_busy = trace->ipc_busy;
	out->ipc_failed = trace->ipc_failed;
	out->ipc_released = trace->ipc_released;
	out->ipc_bad_release = trace->ipc_bad_release;
	out->sensor_irq_count = trace->sensor_irq_count;
	out->sensor_poll_fallbacks = trace->sensor_poll_fallbacks;
	out->sensor_fifo_packets = trace->sensor_fifo_packets;
	out->sensor_samples = trace->sensor_samples;
	out->sensor_malformed = trace->sensor_malformed;
	out->sensor_io_errors = trace->sensor_io_errors;
	out->batches_ready = trace->batches_ready;
	out->last_slot = trace->last_slot;
	out->fifo_overflows = trace->fifo_overflows;
	out->slot_waits = trace->slot_waits;

	return out->magic == RF_LINK_LP_TRACE_MAGIC &&
	       out->version == RF_LINK_LP_TRACE_VERSION;
}

#endif /* RF_LINK_LP_TRACE_H_ */
