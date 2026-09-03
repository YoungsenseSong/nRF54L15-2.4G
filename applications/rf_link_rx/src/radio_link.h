#ifndef RF_LINK_RADIO_LINK_H_
#define RF_LINK_RADIO_LINK_H_

#include <stdint.h>
#include <zephyr/kernel.h>

#include "rf_link_proto.h"

struct radio_link_rx_packet {
	uint64_t rx_tick;
	uint16_t length;
	uint16_t reserved;
	struct rf_frame frame;
};

struct radio_link_stats {
	uint32_t rx_events;
	uint32_t rx_frames;
	uint32_t rx_read_errors;
	uint32_t rx_queue_overflow;
	uint32_t rx_queue_high_water;
};

int radio_link_init(void);
int radio_link_receive(struct radio_link_rx_packet *packet, k_timeout_t timeout);
void radio_link_stats_get(struct radio_link_stats *stats);
const char *radio_link_phy_label(void);

BUILD_ASSERT(sizeof(struct radio_link_rx_packet) == 216u,
	     "radio_link_rx_packet layout changed");

#endif /* RF_LINK_RADIO_LINK_H_ */
