#include <zephyr/kernel.h>

#include "adc_sampler.h"
#include "ipc_tx.h"
#include "lp_trace.h"
#include "proto.h"

static uint64_t lp_now_us(void)
{
	return k_cyc_to_us_floor64(k_cycle_get_64());
}

static void lp_wait_next_period(uint64_t *next_deadline_us)
{
#if RF_LINK_LP_USE_ABSOLUTE_PACING
	uint64_t now_us;
	uint64_t periods_late;
	int64_t sleep_us;

	if (next_deadline_us == NULL) {
		return;
	}

	*next_deadline_us += RF_LINK_TX_PERIOD_US;
	now_us = lp_now_us();
	if (now_us > *next_deadline_us) {
		periods_late = (now_us - *next_deadline_us) / RF_LINK_TX_PERIOD_US;
		*next_deadline_us += periods_late * RF_LINK_TX_PERIOD_US;
	}

	sleep_us = (int64_t)(*next_deadline_us - now_us);
	if (sleep_us > 0) {
		k_sleep(K_USEC((uint32_t)sleep_us));
	}
#else
	ARG_UNUSED(next_deadline_us);
	k_sleep(K_USEC(RF_LINK_TX_PERIOD_US));
#endif
}

int main(void)
{
	struct rf_frame frame;
	struct ipc_tx_stats ipc_stats;
	uint64_t next_deadline_us;
	int ret;

	rf_link_lp_trace_boot();
	adc_sampler_init();

	ret = ipc_tx_init();
	if (ret != 0) {
		rf_link_lp_trace_note_ipc(0u, 0u, 1u, ret);
		return ret;
	}
	rf_link_lp_trace_set_stage(RF_LINK_LP_STAGE_IPC_READY);

	ret = ipc_tx_wait_bound(K_FOREVER);
	if (ret != 0) {
		rf_link_lp_trace_note_ipc(0u, 0u, 1u, ret);
		return ret;
	}
	rf_link_lp_trace_set_stage(RF_LINK_LP_STAGE_IPC_BOUND);

	if (RF_LINK_LP_START_DELAY_MS > 0u) {
		k_sleep(K_MSEC(RF_LINK_LP_START_DELAY_MS));
	}
	next_deadline_us = lp_now_us();

	while (1) {
		static uint32_t warmup_frames;

		adc_sampler_fill_frame(&frame);
		rf_link_lp_trace_note_loop(frame.seq);
		ret = ipc_tx_send_frame(&frame, K_MSEC(RF_LINK_LP_IPC_SEND_TIMEOUT_MS));
		ipc_tx_stats_get(&ipc_stats);
		rf_link_lp_trace_note_ipc(ipc_stats.sent, ipc_stats.busy,
					  ipc_stats.failed, ret);

		if (warmup_frames < RF_LINK_LP_WARMUP_FRAMES) {
			warmup_frames++;
			k_sleep(K_MSEC(RF_LINK_LP_WARMUP_PERIOD_MS));
			next_deadline_us = lp_now_us();
		} else {
			lp_wait_next_period(&next_deadline_us);
		}
	}
}
