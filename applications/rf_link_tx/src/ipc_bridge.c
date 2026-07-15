#include "ipc_bridge.h"

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
K_MSGQ_DEFINE(batch_msgq, sizeof(struct rf_link_ipc_message),
	      RF_LINK_MEMS_BATCH_SLOT_COUNT, 4);

static struct ipc_ept hp_ept;
static atomic_t received;
static atomic_t bad_size;
static atomic_t bad_message;
static atomic_t queued;
static atomic_t queue_drop;
static atomic_t releases;
static atomic_t release_fail;

static void bridge_bound(void *priv)
{
	ARG_UNUSED(priv);
	k_sem_give(&bound_sem);
}

static void bridge_received(const void *data, size_t len, void *priv)
{
	struct rf_link_ipc_message message;
	int ret;

	ARG_UNUSED(priv);
	atomic_inc(&received);

	if (data == NULL || len != sizeof(message)) {
		atomic_inc(&bad_size);
		return;
	}

	memcpy(&message, data, sizeof(message));
	if (message.magic != RF_LINK_IPC_MAGIC ||
	    message.version != RF_LINK_IPC_VERSION ||
	    message.type != RF_LINK_IPC_BATCH_READY ||
	    message.slot_index >= RF_LINK_MEMS_BATCH_SLOT_COUNT ||
	    message.sample_count != RF_LINK_MEMS_BATCH_SAMPLE_COUNT) {
		atomic_inc(&bad_message);
		return;
	}

	ret = k_msgq_put(&batch_msgq, &message, K_NO_WAIT);
	if (ret == 0) {
		atomic_inc(&queued);
	} else {
		atomic_inc(&queue_drop);
	}
}

static void bridge_error(const char *message, void *priv)
{
	ARG_UNUSED(message);
	ARG_UNUSED(priv);
}

static const struct ipc_ept_cfg hp_ept_cfg = {
	.name = "rf_link_ept",
	.cb = {
		.bound = bridge_bound,
		.received = bridge_received,
		.error = bridge_error,
	},
};

int ipc_bridge_init(void)
{
	const struct device *ipc0 = DEVICE_DT_GET(IPC_NODE);
	int ret;

	if (!device_is_ready(ipc0)) {
		return -ENODEV;
	}

	k_msgq_purge(&batch_msgq);
	atomic_clear(&received);
	atomic_clear(&bad_size);
	atomic_clear(&bad_message);
	atomic_clear(&queued);
	atomic_clear(&queue_drop);
	atomic_clear(&releases);
	atomic_clear(&release_fail);

	ret = ipc_service_open_instance(ipc0);
	if (ret < 0 && ret != -EALREADY) {
		return ret;
	}

	return ipc_service_register_endpoint(ipc0, &hp_ept, &hp_ept_cfg);
}

int ipc_bridge_wait_bound(k_timeout_t timeout)
{
	return k_sem_take(&bound_sem, timeout);
}

int ipc_bridge_wait_batch(struct rf_link_ipc_message *message,
			  k_timeout_t timeout)
{
	if (message == NULL) {
		return -EINVAL;
	}

	return k_msgq_get(&batch_msgq, message, timeout);
}

int ipc_bridge_release_batch(uint32_t slot_index, uint32_t batch_seq,
			     k_timeout_t timeout)
{
	struct rf_link_ipc_message message = {
		.magic = RF_LINK_IPC_MAGIC,
		.version = RF_LINK_IPC_VERSION,
		.type = RF_LINK_IPC_BATCH_RELEASE,
		.slot_index = slot_index,
		.batch_seq = batch_seq,
	};
	k_timepoint_t end = sys_timepoint_calc(timeout);
	int ret;

	do {
		ret = ipc_service_send(&hp_ept, &message, sizeof(message));
		if (ret == 0 || ret == (int)sizeof(message)) {
			atomic_inc(&releases);
			return 0;
		}

		if (ret != -ENOMEM && ret != -EAGAIN &&
		    ret != -EBUSY && ret != -ENOBUFS) {
			break;
		}
		k_sleep(K_USEC(50));
	} while (K_TIMEOUT_EQ(timeout, K_FOREVER) || !sys_timepoint_expired(end));

	atomic_inc(&release_fail);
	return ret;
}

void ipc_bridge_stats_get(struct ipc_bridge_stats *stats)
{
	if (stats == NULL) {
		return;
	}

	stats->received = (uint32_t)atomic_get(&received);
	stats->bad_size = (uint32_t)atomic_get(&bad_size);
	stats->bad_message = (uint32_t)atomic_get(&bad_message);
	stats->queued = (uint32_t)atomic_get(&queued);
	stats->queue_drop = (uint32_t)atomic_get(&queue_drop);
	stats->releases = (uint32_t)atomic_get(&releases);
	stats->release_fail = (uint32_t)atomic_get(&release_fail);
}
