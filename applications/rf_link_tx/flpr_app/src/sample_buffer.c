#include "sample_buffer.h"

#include <errno.h>
#include <zephyr/sys/atomic.h>

#define SAMPLE_BUFFER_DEPTH 8

K_MSGQ_DEFINE(sample_frame_msgq, sizeof(struct rf_frame), SAMPLE_BUFFER_DEPTH, 4);

static atomic_t pushed;
static atomic_t popped;
static atomic_t dropped;

void sample_buffer_init(void)
{
	k_msgq_purge(&sample_frame_msgq);
	atomic_clear(&pushed);
	atomic_clear(&popped);
	atomic_clear(&dropped);
}

int sample_buffer_push(const struct rf_frame *frame)
{
	struct rf_frame discard;
	int ret;

	ret = k_msgq_put(&sample_frame_msgq, frame, K_NO_WAIT);
	if (ret == 0) {
		atomic_inc(&pushed);
		return 0;
	}

	if (ret == -ENOMSG && k_msgq_get(&sample_frame_msgq, &discard, K_NO_WAIT) == 0) {
		atomic_inc(&dropped);
		ret = k_msgq_put(&sample_frame_msgq, frame, K_NO_WAIT);
		if (ret == 0) {
			atomic_inc(&pushed);
			return 0;
		}
	}

	atomic_inc(&dropped);
	return ret;
}

int sample_buffer_pop(struct rf_frame *frame, k_timeout_t timeout)
{
	int ret = k_msgq_get(&sample_frame_msgq, frame, timeout);

	if (ret == 0) {
		atomic_inc(&popped);
	}

	return ret;
}

void sample_buffer_stats_get(struct sample_buffer_stats *stats)
{
	if (stats == NULL) {
		return;
	}

	stats->pushed = (uint32_t)atomic_get(&pushed);
	stats->popped = (uint32_t)atomic_get(&popped);
	stats->dropped = (uint32_t)atomic_get(&dropped);
}
