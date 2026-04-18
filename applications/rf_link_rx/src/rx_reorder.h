#ifndef RF_LINK_RX_REORDER_H_
#define RF_LINK_RX_REORDER_H_

#include <stdint.h>

#include "proto.h"

struct rx_reorder_stats {
	uint32_t frames;
	uint32_t samples;
	uint32_t bytes;
	uint32_t lost_frames;
	uint32_t duplicates;
	uint32_t bad_magic;
	uint32_t bad_size;
	uint16_t last_seq;
	int16_t last_first_sample;
	int16_t last_last_sample;
};

void rx_reorder_init(void);
int rx_reorder_process_frame(const struct rf_frame *frame, uint8_t len);
void rx_reorder_stats_get(struct rx_reorder_stats *stats);

#endif /* RF_LINK_RX_REORDER_H_ */
