#include "ipc_bridge.h"

#include <errno.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/ipc/ipc_service.h>
#include <zephyr/sys/atomic.h>

#include "proto.h"
#include "tx_queue.h"

#define IPC_NODE DT_NODELABEL(ipc0)

#if !DT_NODE_HAS_STATUS(IPC_NODE, okay)
#error "ipc0 is not enabled in devicetree"
#endif

static K_SEM_DEFINE(bound_sem, 0, 1);
static struct ipc_ept hp_ept;

static atomic_t received;
static atomic_t bad_size;
static atomic_t bad_magic;
static atomic_t queued;
static atomic_t queue_drop;

static struct rf_frame rx_frame_copy;

static void bridge_bound(void *priv)
{
	ARG_UNUSED(priv);
	k_sem_give(&bound_sem);
}

static void bridge_received(const void *data, size_t len, void *priv)
{
	int ret;

	ARG_UNUSED(priv);
	atomic_inc(&received);

	if (len != sizeof(rx_frame_copy)) {
		atomic_inc(&bad_size);
		return;
	}

	memcpy(&rx_frame_copy, data, sizeof(rx_frame_copy));
	if (rx_frame_copy.magic != RF_LINK_MAGIC ||
	    rx_frame_copy.sample_count > RF_LINK_FRAME_SAMPLE_COUNT) {
		atomic_inc(&bad_magic);
		return;
	}

	ret = tx_queue_submit(&rx_frame_copy);
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

void ipc_bridge_stats_get(struct ipc_bridge_stats *stats)
{
	if (stats == NULL) {
		return;
	}

	stats->received = (uint32_t)atomic_get(&received);
	stats->bad_size = (uint32_t)atomic_get(&bad_size);
	stats->bad_magic = (uint32_t)atomic_get(&bad_magic);
	stats->queued = (uint32_t)atomic_get(&queued);
	stats->queue_drop = (uint32_t)atomic_get(&queue_drop);
}
