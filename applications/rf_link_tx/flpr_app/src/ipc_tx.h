#ifndef RF_LINK_IPC_TX_H_
#define RF_LINK_IPC_TX_H_

#include <stdint.h>
#include <zephyr/kernel.h>

#include "mems_batch.h"

struct ipc_tx_stats {
	uint32_t sent;
	uint32_t busy;
	uint32_t failed;
	uint32_t released;
	uint32_t bad_release;
	uint32_t slot_waits;
};

int ipc_tx_init(void);
int ipc_tx_wait_bound(k_timeout_t timeout);
int ipc_tx_acquire_slot(uint32_t *slot_index, k_timeout_t timeout);
int ipc_tx_publish_batch(uint32_t slot_index, uint32_t batch_seq,
			 uint32_t sample_crc32, k_timeout_t timeout);
void ipc_tx_stats_get(struct ipc_tx_stats *stats);

#endif /* RF_LINK_IPC_TX_H_ */
