#ifndef RF_LINK_LP_TRACE_H_
#define RF_LINK_LP_TRACE_H_

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>

#define RF_LINK_LP_TRACE_MAGIC   0x4c505452u
#define RF_LINK_LP_TRACE_VERSION 1u             //默认是 deadline 模式，要改回相对 k_sleep() 版本置0

#define RF_LINK_LP_TRACE_NODE DT_NODELABEL(rf_link_lp_trace_mem)

#if !DT_NODE_HAS_STATUS(RF_LINK_LP_TRACE_NODE, okay)
#error "rf_link_lp_trace_mem is not enabled in devicetree"
#endif

enum rf_link_lp_stage {
	RF_LINK_LP_STAGE_RESET = 0u,
	RF_LINK_LP_STAGE_BOOT = 1u,
	RF_LINK_LP_STAGE_IPC_READY = 2u,
	RF_LINK_LP_STAGE_IPC_BOUND = 3u,
	RF_LINK_LP_STAGE_RUN = 4u,
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
}

static inline void rf_link_lp_trace_set_stage(uint32_t stage)
{
	volatile struct rf_link_lp_trace *trace = rf_link_lp_trace_ptr();

	trace->stage = stage;
	trace->last_uptime_ms = k_uptime_get_32();
}

static inline void rf_link_lp_trace_note_loop(uint16_t seq)
{
	volatile struct rf_link_lp_trace *trace = rf_link_lp_trace_ptr();

	trace->stage = RF_LINK_LP_STAGE_RUN;
	trace->loop_count++;
	trace->last_seq = seq;
	trace->last_uptime_ms = k_uptime_get_32();
}

static inline void rf_link_lp_trace_note_ipc(uint32_t sent, uint32_t busy,
					     uint32_t failed, int32_t last_ret)
{
	volatile struct rf_link_lp_trace *trace = rf_link_lp_trace_ptr();

	trace->ipc_sent = sent;
	trace->ipc_busy = busy;
	trace->ipc_failed = failed;
	trace->last_send_ret = last_ret;
	trace->last_uptime_ms = k_uptime_get_32();
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

	return out->magic == RF_LINK_LP_TRACE_MAGIC &&
	       out->version == RF_LINK_LP_TRACE_VERSION;
}

#endif /* RF_LINK_LP_TRACE_H_ */
