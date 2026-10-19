#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <esb.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/nrf_clock_control.h>
#include <zephyr/sys/notify.h>
#include <helpers/nrfx_gppi.h>
#include <nrfx_gpiote.h>
#include <nrfx_timer.h>
#include <nrf.h>
#include <nrf_erratas.h>
#if NRF54L_ERRATA_20_PRESENT
#include <hal/nrf_power.h>
#endif
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#define SYNC_MAGIC 0x534eu
#define SYNC_VERSION 1u
#define SYNC_PIPE 0u
#define SYNC_PREFIX 0x54u
#define SYNC_PULSE_WIDTH_US 40u

struct sync_frame {
	uint16_t magic;
	uint8_t version;
	uint8_t reserved;
	uint32_t epoch;
	uint32_t sequence;
	uint32_t pulse_delay_us;
} __packed;

BUILD_ASSERT(sizeof(struct sync_frame) <= CONFIG_ESB_MAX_PAYLOAD_LENGTH,
	     "sync frame exceeds ESB payload limit");

static struct k_sem tx_done;
static volatile uint32_t radio_events;
static volatile uint32_t good_frames;
static volatile uint32_t bad_frames;
static volatile uint32_t tx_ok;
static volatile uint32_t tx_fail;
static volatile uint32_t last_epoch;
static volatile uint32_t last_sequence;
static volatile uint32_t pulse_events;

static nrfx_timer_t sync_timer = NRFX_TIMER_INSTANCE(20);
static nrfx_gpiote_t sync_gpiote = NRFX_GPIOTE_INSTANCE(20);
static uint8_t sync_gpiote_channel;
static uint8_t timer_radio_channel;
static uint8_t timer_gpio_channel;

static void timer_handler(nrf_timer_event_t event_type, void *context)
{
	ARG_UNUSED(context);
	if (event_type == NRF_TIMER_EVENT_COMPARE0) {
		pulse_events++;
	}
}

#if defined(CONFIG_CLOCK_CONTROL_NRF)
static int clocks_start(void)
{
	int err;
	int result;
	struct onoff_manager *manager;
	struct onoff_client client;

	manager = z_nrf_clock_control_get_onoff(CLOCK_CONTROL_NRF_SUBSYS_HF);
	if (manager == NULL) {
		return -ENXIO;
	}
	sys_notify_init_spinwait(&client.notify);
	err = onoff_request(manager, &client);
	if (err < 0) {
		return err;
	}
	do {
		err = sys_notify_fetch_result(&client.notify, &result);
		if (err == 0 && result != 0) {
			return result;
		}
	} while (err != 0);
#if NRF54L_ERRATA_20_PRESENT
	if (nrf54l_errata_20()) {
		nrf_power_task_trigger(NRF_POWER, NRF_POWER_TASK_CONSTLAT);
	}
#endif
	return 0;
}
#elif defined(CONFIG_CLOCK_CONTROL_NRF2)
static int clocks_start(void)
{
	int err;
	int result;
	const struct device *radio_clock =
		DEVICE_DT_GET_OR_NULL(DT_CLOCKS_CTLR(DT_NODELABEL(radio)));
	struct onoff_client client;

	if (radio_clock == NULL) {
		return -ENODEV;
	}
	sys_notify_init_spinwait(&client.notify);
	err = nrf_clock_control_request(radio_clock, NULL, &client);
	if (err < 0 && err != -EALREADY) {
		return err;
	}
	do {
		err = sys_notify_fetch_result(&client.notify, &result);
		if (err == 0 && result != 0) {
			return result;
		}
	} while (err == -EAGAIN);
	return 0;
}
#else
static int clocks_start(void)
{
	return -ENOTSUP;
}
#endif

static void radio_event_handler(const struct esb_evt *event)
{
	struct esb_payload payload;

	if (event == NULL) {
		return;
	}

	if (event->evt_id == ESB_EVENT_TX_SUCCESS) {
		tx_ok++;
		k_sem_give(&tx_done);
		return;
	}
	if (event->evt_id == ESB_EVENT_TX_FAILED) {
		tx_fail++;
		k_sem_give(&tx_done);
		return;
	}
	if (event->evt_id != ESB_EVENT_RX_RECEIVED) {
		return;
	}

	radio_events++;
	while (esb_read_rx_payload(&payload) == 0) {
		struct sync_frame frame = {0};
		size_t copy_len = MIN((size_t)payload.length, sizeof(frame));

		memcpy(&frame, payload.data, copy_len);
		if (copy_len != sizeof(frame) || frame.magic != SYNC_MAGIC ||
		    frame.version != SYNC_VERSION) {
			bad_frames++;
			continue;
		}
		good_frames++;
		last_epoch = frame.epoch;
		last_sequence = frame.sequence;
	}
}

static int radio_init(void)
{
	static const uint8_t base_addr_0[4] = {0x52, 0x46, 0x4c, 0x31};
	static const uint8_t base_addr_1[4] = {0xc2, 0xc2, 0xc2, 0xc2};
	static const uint8_t prefixes[8] = {
		SYNC_PREFIX, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8
	};
	struct esb_config config = ESB_DEFAULT_CONFIG;
	int ret;

	ret = clocks_start();
	if (ret != 0) {
		return ret;
	}
	config.protocol = ESB_PROTOCOL_ESB_DPL;
	config.mode = IS_ENABLED(CONFIG_SYNC_MINIMAL_COORDINATOR) ?
		ESB_MODE_PTX : ESB_MODE_PRX;
	config.bitrate = ESB_BITRATE_2MBPS;
	config.crc = ESB_CRC_16BIT;
	config.event_handler = radio_event_handler;
	config.payload_length = sizeof(struct sync_frame);
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
	ret = esb_set_prefixes(prefixes, ARRAY_SIZE(prefixes));
	if (ret != 0) {
		return ret;
	}
	ret = esb_set_rf_channel(CONFIG_SYNC_MINIMAL_CHANNEL);
	if (ret != 0) {
		return ret;
	}
	if (IS_ENABLED(CONFIG_SYNC_MINIMAL_COORDINATOR)) {
		return esb_flush_tx();
	}
	ret = esb_flush_rx();
	if (ret != 0) {
		return ret;
	}
	return esb_start_rx();
}

static int hardware_pulse_init(void)
{
	nrfx_timer_config_t timer_config =
		NRFX_TIMER_DEFAULT_CONFIG(NRFX_MHZ_TO_HZ(1));
	nrfx_gpiote_output_config_t output_config =
		NRFX_GPIOTE_DEFAULT_OUTPUT_CONFIG;
	nrfx_gpiote_task_config_t task_config = {
		.task_ch = 0,
		.polarity = NRF_GPIOTE_POLARITY_TOGGLE,
		.init_val = NRF_GPIOTE_INITIAL_VALUE_LOW,
	};
	uint32_t radio_event;
	uint32_t timer_clear;
	uint32_t timer_start;
	uint32_t timer_compare;
	uint32_t gpio_task;
	nrfx_err_t err;

	timer_config.bit_width = NRF_TIMER_BIT_WIDTH_32;
	err = nrfx_timer_init(&sync_timer, &timer_config, timer_handler);
	if (err != NRFX_SUCCESS) {
		return -EIO;
	}
	nrfx_timer_extended_compare(
		&sync_timer, NRF_TIMER_CC_CHANNEL0,
		CONFIG_SYNC_MINIMAL_PULSE_DELAY_US,
		NRF_TIMER_SHORT_COMPARE0_STOP_MASK, true);

	if (!nrfx_gpiote_init_check(&sync_gpiote)) {
		err = nrfx_gpiote_init(&sync_gpiote,
				       NRFX_GPIOTE_DEFAULT_CONFIG_IRQ_PRIORITY);
		if (err != NRFX_SUCCESS) {
			return -EIO;
		}
	}
	err = nrfx_gpiote_channel_alloc(&sync_gpiote,
					&sync_gpiote_channel);
	if (err != NRFX_SUCCESS) {
		return -ENOMEM;
	}
	task_config.task_ch = sync_gpiote_channel;
	err = nrfx_gpiote_output_configure(
		&sync_gpiote, CONFIG_SYNC_MINIMAL_PULSE_PIN,
		&output_config, &task_config);
	if (err != NRFX_SUCCESS) {
		return -EIO;
	}
	nrfx_gpiote_out_task_enable(&sync_gpiote,
				    CONFIG_SYNC_MINIMAL_PULSE_PIN);

	radio_event = nrf_radio_event_address_get(
		NRF_RADIO, NRF_RADIO_EVENT_ADDRESS);
	timer_clear = nrfx_timer_task_address_get(
		&sync_timer, NRF_TIMER_TASK_CLEAR);
	timer_start = nrfx_timer_task_address_get(
		&sync_timer, NRF_TIMER_TASK_START);
	timer_compare = nrfx_timer_compare_event_address_get(
		&sync_timer, NRF_TIMER_CC_CHANNEL0);
	gpio_task = nrfx_gpiote_out_task_address_get(
		&sync_gpiote, CONFIG_SYNC_MINIMAL_PULSE_PIN);

	err = nrfx_gppi_channel_alloc(&timer_radio_channel);
	if (err != NRFX_SUCCESS) {
		return -ENOMEM;
	}
	nrfx_gppi_channel_endpoints_setup(timer_radio_channel,
					  radio_event, timer_clear);
	nrfx_gppi_fork_endpoint_setup(timer_radio_channel, timer_start);
	nrfx_gppi_channels_enable(BIT(timer_radio_channel));

	err = nrfx_gppi_channel_alloc(&timer_gpio_channel);
	if (err != NRFX_SUCCESS) {
		return -ENOMEM;
	}
	nrfx_gppi_channel_endpoints_setup(timer_gpio_channel,
					  timer_compare, gpio_task);
	nrfx_gppi_channels_enable(BIT(timer_gpio_channel));
	return 0;
}

static void print_status(uint32_t epoch)
{
	printk("SYNC role=%s epoch=%u tx_ok=%u tx_fail=%u radio_events=%u "
	       "frames_ok=%u frames_bad=%u last_epoch=%u last_seq=%u "
	       "pulse_events=%u pulse_pin=P1.09 pulse_delay_us=%u\n",
	       IS_ENABLED(CONFIG_SYNC_MINIMAL_COORDINATOR) ? "COORD" : "RESP",
	       epoch, tx_ok, tx_fail, radio_events, good_frames, bad_frames,
	       last_epoch, last_sequence, pulse_events,
	       CONFIG_SYNC_MINIMAL_PULSE_DELAY_US);
}

int main(void)
{
	uint32_t epoch = 1u;
	uint32_t sequence = 0u;
	int ret;

	k_sem_init(&tx_done, 0, 1);
	ret = hardware_pulse_init();
	if (ret != 0) {
		printk("SYNC hardware pulse init failed err=%d\n", ret);
		return ret;
	}
	ret = radio_init();
	if (ret != 0) {
		printk("SYNC radio init failed err=%d channel=%d\n",
		       ret, CONFIG_SYNC_MINIMAL_CHANNEL);
		return ret;
	}

	printk("SYNC_MINIMAL role=%s channel=%d address=0x54 pulse=P1.09 "
	       "delay_us=%d\n",
	       IS_ENABLED(CONFIG_SYNC_MINIMAL_COORDINATOR) ? "COORD" : "RESP",
	       CONFIG_SYNC_MINIMAL_CHANNEL, CONFIG_SYNC_MINIMAL_PULSE_DELAY_US);

	if (IS_ENABLED(CONFIG_SYNC_MINIMAL_COORDINATOR)) {
		while (true) {
			struct sync_frame frame = {
				.magic = SYNC_MAGIC,
				.version = SYNC_VERSION,
				.epoch = epoch,
				.sequence = sequence++,
				.pulse_delay_us = CONFIG_SYNC_MINIMAL_PULSE_DELAY_US,
			};
			struct esb_payload payload = {
				.pipe = SYNC_PIPE,
				.noack = true,
				.length = sizeof(frame),
			};

			memcpy(payload.data, &frame, sizeof(frame));
			ret = esb_write_payload(&payload);
			if (ret == 0) {
				(void)k_sem_take(&tx_done, K_MSEC(10));
			} else {
				tx_fail++;
			}
			if ((sequence % 10u) == 0u) {
				print_status(epoch);
			}
			k_sleep(K_MSEC(CONFIG_SYNC_MINIMAL_PERIOD_MS));
		}
	}

	while (true) {
		print_status(epoch);
		k_sleep(K_SECONDS(1));
	}
	return 0;
}
