#include "ipc_tx.h"

#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/ipc/ipc_service.h>
#include <zephyr/sys/atomic.h>

#define IPC_NODE DT_NODELABEL(ipc0)

#if !DT_NODE_HAS_STATUS(IPC_NODE, okay)
#error "ipc0 is not enabled in devicetree"
#endif

static K_SEM_DEFINE(bound_sem, 0, 1);
static struct ipc_ept lp_ept;

static atomic_t sent;
static atomic_t busy;
static atomic_t failed;

static void tx_bound(void *priv)
{
	ARG_UNUSED(priv);
	k_sem_give(&bound_sem);
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

int ipc_tx_send_frame(const struct rf_frame *frame, k_timeout_t timeout)
{
	k_timepoint_t end;
	int ret;

	if (frame == NULL) {
		return -EINVAL;
	}

	end = sys_timepoint_calc(timeout);

	do {
		ret = ipc_service_send(&lp_ept, frame, sizeof(*frame));
		if (ret == 0 || ret == (int)sizeof(*frame)) {
			atomic_inc(&sent);
			return 0;
		}

		if (ret != -ENOMEM && ret != -EAGAIN &&
		    ret != -EBUSY && ret != -ENOBUFS) {
			atomic_inc(&failed);
			return ret;
		}

		atomic_inc(&busy);
		k_sleep(K_USEC(50));
	} while (K_TIMEOUT_EQ(timeout, K_FOREVER) || !sys_timepoint_expired(end));

	atomic_inc(&failed);
	return -ETIMEDOUT;
}

void ipc_tx_stats_get(struct ipc_tx_stats *stats)
{
	if (stats == NULL) {
		return;
	}

	stats->sent = (uint32_t)atomic_get(&sent);
	stats->busy = (uint32_t)atomic_get(&busy);
	stats->failed = (uint32_t)atomic_get(&failed);
}
