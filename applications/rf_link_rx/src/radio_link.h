#ifndef RF_LINK_RADIO_LINK_H_
#define RF_LINK_RADIO_LINK_H_

#include <stdint.h>

struct radio_link_stats {
	uint32_t rx_events;
	uint32_t rx_frames;
	uint32_t rx_read_errors;
};

int radio_link_init(void);
void radio_link_stats_get(struct radio_link_stats *stats);
const char *radio_link_phy_label(void);

#endif /* RF_LINK_RADIO_LINK_H_ */
