#include "radio_link.h"

#include <errno.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/nrf_clock_control.h>
#include <zephyr/sys/atomic.h>
#include <esb.h>
#include <nrf.h>
#include <nrf_erratas.h>

#include "proto.h"
#include "rx_reorder.h"

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

static atomic_t rx_events;
static atomic_t rx_frames;
static atomic_t rx_read_errors;

static void radio_event_handler(const struct esb_evt *event)
{
	struct esb_payload payload;

	switch (event->evt_id) {
	case ESB_EVENT_TX_SUCCESS:
	case ESB_EVENT_TX_FAILED:
		break;
	case ESB_EVENT_RX_RECEIVED:
		atomic_inc(&rx_events);
		while (esb_read_rx_payload(&payload) == 0) {
			(void)rx_reorder_process_frame((const struct rf_frame *)payload.data,
						       payload.length);
			atomic_inc(&rx_frames);
		}
		break;
	default:
		atomic_inc(&rx_read_errors);
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
	config.mode = ESB_MODE_PRX;
	config.bitrate = RF_LINK_ESB_BITRATE;
	config.crc = ESB_CRC_16BIT;
	config.event_handler = radio_event_handler;
	config.payload_length = RF_LINK_FRAME_WIRE_SIZE;
	config.selective_auto_ack = true;

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

	ret = esb_flush_rx();
	if (ret != 0) {
		return ret;
	}

	return esb_start_rx();
}

void radio_link_stats_get(struct radio_link_stats *stats)
{
	if (stats == NULL) {
		return;
	}

	stats->rx_events = (uint32_t)atomic_get(&rx_events);
	stats->rx_frames = (uint32_t)atomic_get(&rx_frames);
	stats->rx_read_errors = (uint32_t)atomic_get(&rx_read_errors);
}

const char *radio_link_phy_label(void)
{
	return RF_LINK_PHY_LABEL;
}
