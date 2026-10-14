#include "ipc_tx.h"

#include <errno.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/ipc/ipc_service.h>
#include <zephyr/sys/atomic.h>

#define IPC_NODE DT_NODELABEL(ipc0)

#if !DT_NODE_HAS_STATUS(IPC_NODE, okay)
#error "ipc0 is not enabled in devicetree"
#endif

static K_SEM_DEFINE(bound_sem, 0, 1);
K_MSGQ_DEFINE(free_slot_msgq, sizeof(uint32_t), RF_LINK_MEMS_BATCH_SLOT_COUNT, 4);

static struct ipc_ept lp_ept;
static uint32_t slot_batch_seq[RF_LINK_MEMS_BATCH_SLOT_COUNT];
static atomic_t slot_inflight[RF_LINK_MEMS_BATCH_SLOT_COUNT];
static atomic_t sent;
static atomic_t busy;
static atomic_t failed;
static atomic_t released;
static atomic_t bad_release;
static atomic_t slot_waits;

static void tx_bound(void *priv)
{
	ARG_UNUSED(priv);
	k_sem_give(&bound_sem);
}

static void tx_received(const void *data, size_t len, void *priv)
{
	struct rf_link_ipc_message message;
	uint32_t slot;

	ARG_UNUSED(priv);

	if (data == NULL || len != sizeof(message)) {
		atomic_inc(&bad_release);
		return;
	}

	memcpy(&message, data, sizeof(message));
	slot = message.slot_index;
	if (message.magic != RF_LINK_IPC_MAGIC ||
	    message.version != RF_LINK_IPC_VERSION ||
	    message.type != RF_LINK_IPC_BATCH_RELEASE ||
	    slot >= RF_LINK_MEMS_BATCH_SLOT_COUNT ||
	    atomic_get(&slot_inflight[slot]) == 0 ||
	    slot_batch_seq[slot] != message.batch_seq) {
		atomic_inc(&bad_release);
		return;
	}

	atomic_clear(&slot_inflight[slot]);
	if (k_msgq_put(&free_slot_msgq, &slot, K_NO_WAIT) == 0) {
		atomic_inc(&released);
	} else {
		atomic_inc(&bad_release);
	}
}

static void tx_error(const char *message, void *priv)
{
	ARG_UNUSED(message);
	ARG_UNUSED(priv);
}

static const struct ipc_ept_cfg lp_ept_cfg = {
	.name = "rf_link_ept",
	.cb = {
		.bound = tx_bound,
		.received = tx_received,
		.error = tx_error,
	},
};

int ipc_tx_init(void)
{
	const struct device *ipc0 = DEVICE_DT_GET(IPC_NODE);
	int ret;

	if (!device_is_ready(ipc0)) {
		return -ENODEV;
	}

	k_msgq_purge(&free_slot_msgq);
	for (uint32_t slot = 0u; slot < RF_LINK_MEMS_BATCH_SLOT_COUNT; slot++) {
		slot_batch_seq[slot] = 0u;
		atomic_clear(&slot_inflight[slot]);
		ret = k_msgq_put(&free_slot_msgq, &slot, K_NO_WAIT);
		if (ret != 0) {
			return ret;
		}
	}
	atomic_clear(&sent);
	atomic_clear(&busy);
	atomic_clear(&failed);
	atomic_clear(&released);
	atomic_clear(&bad_release);
	atomic_clear(&slot_waits);

	ret = ipc_service_open_instance(ipc0);
	if (ret < 0 && ret != -EALREADY) {
		return ret;
	}

	return ipc_service_register_endpoint(ipc0, &lp_ept, &lp_ept_cfg);
}

int ipc_tx_wait_bound(k_timeout_t timeout)
{
	return k_sem_take(&bound_sem, timeout);
}

int ipc_tx_acquire_slot(uint32_t *slot_index, k_timeout_t timeout)
{
	int ret;

	if (slot_index == NULL) {
		return -EINVAL;
	}

	ret = k_msgq_get(&free_slot_msgq, slot_index, K_NO_WAIT);
	if (ret == 0 || K_TIMEOUT_EQ(timeout, K_NO_WAIT)) {
		return ret;
	}

	atomic_inc(&slot_waits);
	return k_msgq_get(&free_slot_msgq, slot_index, timeout);
}

int ipc_tx_publish_batch(uint32_t slot_index, uint32_t batch_seq,
			 uint32_t sample_crc32, k_timeout_t timeout)
{
	struct rf_link_ipc_message message = {
		.magic = RF_LINK_IPC_MAGIC,
		.version = RF_LINK_IPC_VERSION,
		.type = RF_LINK_IPC_BATCH_READY,
		.slot_index = slot_index,
		.batch_seq = batch_seq,
		.sample_count = RF_LINK_MEMS_BATCH_SAMPLE_COUNT,
		.sample_crc32 = sample_crc32,
	};
	k_timepoint_t end;
	int ret;

	if (slot_index >= RF_LINK_MEMS_BATCH_SLOT_COUNT) {
		return -EINVAL;
	}

	slot_batch_seq[slot_index] = batch_seq;
	atomic_set(&slot_inflight[slot_index], 1);
	end = sys_timepoint_calc(timeout);

	do {
		ret = ipc_service_send(&lp_ept, &message, sizeof(message));
		if (ret == 0 || ret == (int)sizeof(message)) {
			atomic_inc(&sent);
			return 0;
		}

		if (ret != -ENOMEM && ret != -EAGAIN &&
		    ret != -EBUSY && ret != -ENOBUFS) {
			break;
		}
		atomic_inc(&busy);
		k_sleep(K_USEC(50));
	} while (K_TIMEOUT_EQ(timeout, K_FOREVER) || !sys_timepoint_expired(end));

	atomic_clear(&slot_inflight[slot_index]);
	(void)k_msgq_put(&free_slot_msgq, &slot_index, K_NO_WAIT);
	atomic_inc(&failed);
	return ret;
}

void ipc_tx_stats_get(struct ipc_tx_stats *stats)
{
	if (stats == NULL) {
		return;
	}

	stats->sent = (uint32_t)atomic_get(&sent);
	stats->busy = (uint32_t)atomic_get(&busy);
	stats->failed = (uint32_t)atomic_get(&failed);
	stats->released = (uint32_t)atomic_get(&released);
	stats->bad_release = (uint32_t)atomic_get(&bad_release);
	stats->slot_waits = (uint32_t)atomic_get(&slot_waits);
}
