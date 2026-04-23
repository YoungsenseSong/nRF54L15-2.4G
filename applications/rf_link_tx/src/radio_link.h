#ifndef RF_LINK_RADIO_LINK_H_
#define RF_LINK_RADIO_LINK_H_

#include <stdint.h>
#include <zephyr/kernel.h>

#include "proto.h"

struct radio_link_stats {
	uint32_t tx_ok;
	uint32_t tx_failed;
	uint32_t tx_timeout;
	uint32_t tx_errors;
	uint32_t last_attempts;
	uint32_t mac_latency_count;
	uint32_t mac_latency_last_us;
	uint32_t mac_latency_min_us;
	uint32_t mac_latency_avg_us;
	uint32_t mac_latency_max_us;
};

int radio_link_init(void);
int radio_link_send_frame(const struct rf_frame *frame, k_timeout_t timeout);
void radio_link_stats_get(struct radio_link_stats *stats);
const char *radio_link_phy_label(void);

#endif /* RF_LINK_RADIO_LINK_H_ */
