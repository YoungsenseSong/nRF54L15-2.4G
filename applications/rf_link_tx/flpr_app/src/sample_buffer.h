#ifndef RF_LINK_SAMPLE_BUFFER_H_
#define RF_LINK_SAMPLE_BUFFER_H_

#include <stdint.h>
#include <zephyr/kernel.h>

#include "proto.h"

struct sample_buffer_stats {
	uint32_t pushed;
	uint32_t popped;
	uint32_t dropped;
};

void sample_buffer_init(void);
int sample_buffer_push(const struct rf_frame *frame);
int sample_buffer_pop(struct rf_frame *frame, k_timeout_t timeout);
void sample_buffer_stats_get(struct sample_buffer_stats *stats);

#endif /* RF_LINK_SAMPLE_BUFFER_H_ */
