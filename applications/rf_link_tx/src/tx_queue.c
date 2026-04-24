#include "tx_queue.h"

#include <errno.h>
#include <string.h>
#include <zephyr/sys/atomic.h>

#define TX_QUEUE_DEPTH 64

K_MSGQ_DEFINE(tx_frame_msgq, sizeof(struct rf_frame), TX_QUEUE_DEPTH, 4);

static struct rf_frame discard_frame;

static atomic_t pushed;
static atomic_t popped;
static atomic_t dropped;

void tx_queue_init(void)
{
	k_msgq_purge(&tx_frame_msgq);
	atomic_clear(&pushed);
	atomic_clear(&popped);
	atomic_clear(&dropped);
}

int tx_queue_submit(const struct rf_frame *frame)
{
	int ret;

	ret = k_msgq_put(&tx_frame_msgq, frame, K_NO_WAIT);
	if (ret == 0) {
		atomic_inc(&pushed);
		return 0;
	}

	if (ret == -ENOMSG) {
		ret = k_msgq_get(&tx_frame_msgq, &discard_frame, K_NO_WAIT);
		if (ret == 0) {
			atomic_inc(&dropped);
			ret = k_msgq_put(&tx_frame_msgq, frame, K_NO_WAIT);
			if (ret == 0) {
				atomic_inc(&pushed);
				return 0;
			}
		}
	}

	atomic_inc(&dropped);
	return ret;
}

int tx_queue_get(struct rf_frame *frame, k_timeout_t timeout)
{
	int ret = k_msgq_get(&tx_frame_msgq, frame, timeout);

	if (ret == 0) {
		atomic_inc(&popped);
	}

	return ret;
}

void tx_queue_stats_get(struct tx_queue_stats *stats)
{
	if (stats == NULL) {
		return;
	}

	stats->pushed = (uint32_t)atomic_get(&pushed);
	stats->popped = (uint32_t)atomic_get(&popped);
	stats->dropped = (uint32_t)atomic_get(&dropped);
}
