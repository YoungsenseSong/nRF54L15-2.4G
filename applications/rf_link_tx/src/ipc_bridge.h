#ifndef RF_LINK_IPC_BRIDGE_H_
#define RF_LINK_IPC_BRIDGE_H_

#include <stdint.h>
#include <zephyr/kernel.h>

struct ipc_bridge_stats {
	uint32_t received;
	uint32_t bad_size;
	uint32_t bad_magic;
	uint32_t queued;
	uint32_t queue_drop;
};

int ipc_bridge_init(void);
int ipc_bridge_wait_bound(k_timeout_t timeout);
void ipc_bridge_stats_get(struct ipc_bridge_stats *stats);

#endif /* RF_LINK_IPC_BRIDGE_H_ */
