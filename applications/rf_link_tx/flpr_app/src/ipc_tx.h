#ifndef RF_LINK_IPC_TX_H_
#define RF_LINK_IPC_TX_H_

#include <stdint.h>
#include <zephyr/kernel.h>

#include "proto.h"

struct ipc_tx_stats {
	uint32_t sent;
	uint32_t busy;
	uint32_t failed;
};

int ipc_tx_init(void);
int ipc_tx_wait_bound(k_timeout_t timeout);
int ipc_tx_send_frame(const struct rf_frame *frame, k_timeout_t timeout);
void ipc_tx_stats_get(struct ipc_tx_stats *stats);

#endif /* RF_LINK_IPC_TX_H_ */
