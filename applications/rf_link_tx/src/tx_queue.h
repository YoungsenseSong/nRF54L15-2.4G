#ifndef RF_LINK_TX_QUEUE_H_
#define RF_LINK_TX_QUEUE_H_

#include <stdint.h>
#include <zephyr/kernel.h>

#include "proto.h"

struct tx_queue_stats {
	uint32_t pushed;
	uint32_t popped;
	uint32_t dropped;
};

void tx_queue_init(void);
int tx_queue_submit(const struct rf_frame *frame);
int tx_queue_get(struct rf_frame *frame, k_timeout_t timeout);
void tx_queue_stats_get(struct tx_queue_stats *stats);

#endif /* RF_LINK_TX_QUEUE_H_ */
