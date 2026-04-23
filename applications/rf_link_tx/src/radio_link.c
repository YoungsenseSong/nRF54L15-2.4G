#include "radio_link.h"

#include <errno.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/nrf_clock_control.h>
#include <zephyr/spinlock.h>
#include <zephyr/sys/atomic.h>
#include <esb.h>
#include <nrf.h>
#include <nrf_erratas.h>

#if defined(CONFIG_CLOCK_CONTROL_NRF2)
#include <hal/nrf_lrcconf.h>
#endif

#if NRF54L_ERRATA_20_PRESENT
#include <hal/nrf_power.h>
#endif

#if defined(NRF54LM20A_ENGA_XXAA)
#include <hal/nrf_clock.h>
#endif

BUILD_ASSERT(RF_LINK_FRAME_WIRE_SIZE <= CONFIG_ESB_MAX_PAYLOAD_LENGTH,
	     "CONFIG_ESB_MAX_PAYLOAD_LENGTH is too small for rf_frame");

#if defined(RADIO_MODE_MODE_Nrf_4Mbit_0BT6)
#define RF_LINK_ESB_BITRATE ESB_BITRATE_4MBPS
#define RF_LINK_PHY_LABEL   "4M"
#else
#define RF_LINK_ESB_BITRATE ESB_BITRATE_2MBPS
#define RF_LINK_PHY_LABEL   "2M"
#endif

static K_SEM_DEFINE(tx_done_sem, 0, 1);

static atomic_t tx_ok;
static atomic_t tx_failed;
static atomic_t tx_timeout;
static atomic_t tx_errors;
static atomic_t last_attempts;
static volatile int last_tx_result;

static struct k_spinlock latency_lock;
static uint32_t latency_count;
static uint32_t latency_last_us;
static uint32_t latency_min_us = UINT32_MAX;
static uint32_t latency_max_us;
static uint64_t latency_sum_us;

static void latency_stats_update(uint32_t latency_us)
{
	k_spinlock_key_t key = k_spin_lock(&latency_lock);

	latency_last_us = latency_us;
	if (latency_us < latency_min_us) {
		latency_min_us = latency_us;
	}
	if (latency_us > latency_max_us) {
		latency_max_us = latency_us;
	}
	latency_sum_us += latency_us;
	latency_count++;

	k_spin_unlock(&latency_lock, key);
}

static void radio_event_handler(const struct esb_evt *event)
{
	switch (event->evt_id) {
	case ESB_EVENT_TX_SUCCESS:
		last_tx_result = 0;
		atomic_inc(&tx_ok);
		atomic_set(&last_attempts, (atomic_val_t)event->tx_attempts);
		k_sem_give(&tx_done_sem);
		break;
	case ESB_EVENT_TX_FAILED:
		last_tx_result = -EIO;
		atomic_inc(&tx_failed);
		atomic_set(&last_attempts, (atomic_val_t)event->tx_attempts);
		k_sem_give(&tx_done_sem);
		break;
	case ESB_EVENT_RX_RECEIVED:
		break;
	}
}

#if defined(CONFIG_CLOCK_CONTROL_NRF)
static int clocks_start(void)
{
	int err;
	int res;
	struct onoff_manager *clk_mgr;
	struct onoff_client clk_cli;

	clk_mgr = z_nrf_clock_control_get_onoff(CLOCK_CONTROL_NRF_SUBSYS_HF);
	if (clk_mgr == NULL) {
		return -ENXIO;
	}

	sys_notify_init_spinwait(&clk_cli.notify);

	err = onoff_request(clk_mgr, &clk_cli);
	if (err < 0) {
		return err;
	}

	do {
		err = sys_notify_fetch_result(&clk_cli.notify, &res);
		if (err == 0 && res != 0) {
			return res;
		}
	} while (err != 0);

#if NRF54L_ERRATA_20_PRESENT
	if (nrf54l_errata_20()) {
		nrf_power_task_trigger(NRF_POWER, NRF_POWER_TASK_CONSTLAT);
	}
#endif

#if defined(NRF54LM20A_ENGA_XXAA)
	nrf_clock_task_trigger(NRF_CLOCK, NRF_CLOCK_TASK_PLLSTART);
#endif

	return 0;
}
#elif defined(CONFIG_CLOCK_CONTROL_NRF2)
static int clocks_start(void)
{
	int err;
	int res;
	const struct device *radio_clk_dev =
		DEVICE_DT_GET_OR_NULL(DT_CLOCKS_CTLR(DT_NODELABEL(radio)));
	struct onoff_client radio_cli;

	if (radio_clk_dev == NULL) {
		return -ENODEV;
	}

	nrf_lrcconf_poweron_force_set(NRF_LRCCONF010,
				      NRF_LRCCONF_POWER_DOMAIN_1, true);
	sys_notify_init_spinwait(&radio_cli.notify);

	err = nrf_clock_control_request(radio_clk_dev, NULL, &radio_cli);
	if (err < 0 && err != -EALREADY) {
		return err;
	}

	do {
		err = sys_notify_fetch_result(&radio_cli.notify, &res);
		if (err == 0 && res != 0) {
			return res;
		}
	} while (err == -EAGAIN);

	nrf_lrcconf_clock_always_run_force_set(NRF_LRCCONF000, 0, true);
	nrf_lrcconf_task_trigger(NRF_LRCCONF000, NRF_LRCCONF_TASK_CLKSTART_0);

	return 0;
}
#else
BUILD_ASSERT(false, "No Nordic clock control driver enabled");
#endif

int radio_link_init(void)
{
	static const uint8_t base_addr_0[4] = {0x52, 0x46, 0x4c, 0x31};
	static const uint8_t base_addr_1[4] = {0xc2, 0xc2, 0xc2, 0xc2};
	static const uint8_t addr_prefix[8] = {
		0x54, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8
	};
	struct esb_config config = ESB_DEFAULT_CONFIG;
	int ret;

	ret = clocks_start();
	if (ret != 0) {
		return ret;
	}

	config.protocol = ESB_PROTOCOL_ESB_DPL;
	config.mode = ESB_MODE_PTX;
	config.bitrate = RF_LINK_ESB_BITRATE;
	config.crc = ESB_CRC_16BIT;
	config.event_handler = radio_event_handler;
	config.retransmit_delay = 600;
	config.retransmit_count = 0;
	config.payload_length = RF_LINK_FRAME_WIRE_SIZE;
	config.selective_auto_ack = true;
	config.tx_mode = ESB_TXMODE_AUTO;

	ret = esb_init(&config);
	if (ret != 0) {
		return ret;
	}

	ret = esb_set_address_length(5);
	if (ret != 0) {
		return ret;
	}

	ret = esb_set_base_address_0(base_addr_0);
	if (ret != 0) {
		return ret;
	}

	ret = esb_set_base_address_1(base_addr_1);
	if (ret != 0) {
		return ret;
	}

	ret = esb_set_prefixes(addr_prefix, ARRAY_SIZE(addr_prefix));
	if (ret != 0) {
		return ret;
	}

	ret = esb_set_rf_channel(RF_LINK_CHANNEL);
	if (ret != 0) {
		return ret;
	}

	return esb_flush_tx();
}

int radio_link_send_frame(const struct rf_frame *frame, k_timeout_t timeout)
{
	struct esb_payload payload = {0};
	uint32_t start_cycles;
	uint32_t elapsed_cycles;
	int ret;

	if (frame == NULL) {
		return -EINVAL;
	}

	payload.pipe = 0;
	payload.noack = RF_LINK_NOACK_STREAM ? true : false;
	payload.length = RF_LINK_FRAME_WIRE_SIZE;
	memcpy(payload.data, frame, sizeof(*frame));

	k_sem_reset(&tx_done_sem);
	last_tx_result = -EINPROGRESS;

	start_cycles = k_cycle_get_32();

	ret = esb_write_payload(&payload);
	if (ret != 0) {
		atomic_inc(&tx_errors);
		return ret;
	}

	ret = k_sem_take(&tx_done_sem, timeout);
	elapsed_cycles = k_cycle_get_32() - start_cycles;
	latency_stats_update(k_cyc_to_us_floor32(elapsed_cycles));

	if (ret != 0) {
		atomic_inc(&tx_timeout);
		(void)esb_flush_tx();
		return -ETIMEDOUT;
	}

	return last_tx_result;
}

void radio_link_stats_get(struct radio_link_stats *stats)
{
	k_spinlock_key_t key;

	if (stats == NULL) {
		return;
	}

	stats->tx_ok = (uint32_t)atomic_get(&tx_ok);
	stats->tx_failed = (uint32_t)atomic_get(&tx_failed);
	stats->tx_timeout = (uint32_t)atomic_get(&tx_timeout);
	stats->tx_errors = (uint32_t)atomic_get(&tx_errors);
	stats->last_attempts = (uint32_t)atomic_get(&last_attempts);

	key = k_spin_lock(&latency_lock);
	stats->mac_latency_count = latency_count;
	stats->mac_latency_last_us = latency_last_us;
	stats->mac_latency_min_us = (latency_count == 0u) ? 0u : latency_min_us;
	stats->mac_latency_avg_us = (latency_count == 0u) ? 0u :
				    (uint32_t)(latency_sum_us / latency_count);
	stats->mac_latency_max_us = latency_max_us;
	k_spin_unlock(&latency_lock, key);
}

const char *radio_link_phy_label(void)
{
	return RF_LINK_PHY_LABEL;
}
