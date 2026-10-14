#ifndef RF_LINK_RX_REORDER_H_
#define RF_LINK_RX_REORDER_H_

#include <stdint.h>

#include "rf_link_proto.h"
#include "rf_link_future.h"

struct rx_reorder_stats {
	uint32_t frames;
	uint32_t samples;
	uint32_t bytes;
	uint32_t lost_frames;
	uint32_t lost_samples;
	uint32_t duplicates;
	uint32_t late_frames;
	uint32_t index_estimated;
	uint32_t bad_magic;
	uint32_t bad_size;
	uint32_t bad_sample_count;
	uint32_t extended_frame_seq;
	uint16_t last_seq;
	int16_t last_first_sample;
	int16_t last_last_sample;
	struct rx_missing_range last_missing;
};

void rx_reorder_init(void);
int rx_reorder_process_frame(const struct rf_frame *frame, uint8_t len);
int rx_reorder_process_packet(const struct rf_frame *frame, uint16_t len,
			      uint64_t rx_tick,
			      struct rx_frame_record *record);
void rx_reorder_stats_get(struct rx_reorder_stats *stats);

#endif /* RF_LINK_RX_REORDER_H_ */
