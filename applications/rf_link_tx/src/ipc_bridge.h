#ifndef RF_LINK_IPC_BRIDGE_H_
#define RF_LINK_IPC_BRIDGE_H_

#include <stdint.h>
#include <zephyr/kernel.h>

#include "mems_batch.h"

struct ipc_bridge_stats {
	uint32_t received;
	uint32_t bad_size;
	uint32_t bad_message;
	uint32_t queued;
	uint32_t queue_drop;
	uint32_t releases;
	uint32_t release_fail;
};

int ipc_bridge_init(void);
int ipc_bridge_wait_bound(k_timeout_t timeout);
int ipc_bridge_wait_batch(struct rf_link_ipc_message *message,
			  k_timeout_t timeout);
int ipc_bridge_release_batch(uint32_t slot_index, uint32_t batch_seq,
			     k_timeout_t timeout);
void ipc_bridge_stats_get(struct ipc_bridge_stats *stats);

#endif /* RF_LINK_IPC_BRIDGE_H_ */
