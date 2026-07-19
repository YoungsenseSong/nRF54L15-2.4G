#ifndef RF_LINK_FRAME_QUEUE_H_
#define RF_LINK_FRAME_QUEUE_H_

#include <stddef.h>
#include <stdint.h>

#include "rf_link_future.h"

struct frame_queue_stats {
	uint32_t push_total;
	uint32_t pop_total;
	uint32_t overflow;
	uint32_t high_water;
};

void frame_queue_init(void);
int frame_queue_push(const struct rx_frame_record *record);
int frame_queue_peek(struct rx_frame_record *record);
int frame_queue_commit(uint32_t expected_extended_frame_seq);
void frame_queue_clear(void);
size_t frame_queue_level(void);
size_t frame_queue_high_water(void);
void frame_queue_stats_get(struct frame_queue_stats *stats);

#endif /* RF_LINK_FRAME_QUEUE_H_ */
