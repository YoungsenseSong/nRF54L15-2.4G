#include "timebase.h"

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/spinlock.h>
#include <zephyr/sys_clock.h>

#if defined(CONFIG_RF_LINK_SYNC_HW_CAPTURE)
#include <helpers/nrfx_gppi.h>
#include <nrfx_gpiote.h>
#include <nrfx_timer.h>
#include <zephyr/devicetree.h>
#include <zephyr/sys/util.h>

#include "sync_manager.h"

#define CH0_FPGA_IF_NODE DT_NODELABEL(ch0_fpga_if)

BUILD_ASSERT(DT_NODE_HAS_PROP(CH0_FPGA_IF_NODE, sync_gpios),
	     "CH0 overlay must define sync-gpios");

static const nrfx_gpiote_t sync_gpiote = NRFX_GPIOTE_INSTANCE(20);
static const nrfx_timer_t sync_timer = NRFX_TIMER_INSTANCE(20);
static const nrfx_gpiote_pin_t sync_pin =
	NRF_DT_GPIOS_TO_PSEL(CH0_FPGA_IF_NODE, sync_gpios);
static uint8_t sync_gpiote_channel;
static uint8_t sync_gppi_channel;
static uint32_t last_timer_raw;
static uint64_t timer_epoch;
#endif

static struct k_spinlock capture_lock;
static uint64_t last_sync_capture;

#if defined(CONFIG_RF_LINK_SYNC_HW_CAPTURE)
static uint64_t extend_timer_value(uint32_t raw)
{
	k_spinlock_key_t key = k_spin_lock(&capture_lock);

	if (raw < last_timer_raw && (last_timer_raw - raw) > BIT(31)) {
		timer_epoch += BIT64(32);
	}
	last_timer_raw = raw;
	uint64_t value = timer_epoch | raw;
	k_spin_unlock(&capture_lock, key);
	return value;
}

static void sync_pin_handler(nrfx_gpiote_pin_t pin,
			     nrfx_gpiote_trigger_t trigger, void *context)
{
	uint32_t captured;

	ARG_UNUSED(pin);
	ARG_UNUSED(trigger);
	ARG_UNUSED(context);
	captured = nrfx_timer_capture_get(&sync_timer, NRF_TIMER_CC_CHANNEL1);
	sync_manager_on_sync_capture(extend_timer_value(captured));
}

static int hardware_timebase_init(void)
{
	nrfx_timer_config_t timer_config =
		NRFX_TIMER_DEFAULT_CONFIG(NRFX_MHZ_TO_HZ(1));
	nrf_gpio_pin_pull_t pull = NRF_GPIO_PIN_NOPULL;
	nrfx_gpiote_trigger_config_t trigger_config = {
		.trigger = NRFX_GPIOTE_TRIGGER_LOTOHI,
		.p_in_channel = &sync_gpiote_channel,
	};
	nrfx_gpiote_handler_config_t handler_config = {
		.handler = sync_pin_handler,
		.p_context = NULL,
	};
	nrfx_gpiote_input_pin_config_t input_config = {
		.p_pull_config = &pull,
		.p_trigger_config = &trigger_config,
		.p_handler_config = &handler_config,
	};
	nrfx_err_t err;

	timer_config.bit_width = NRF_TIMER_BIT_WIDTH_32;
	err = nrfx_timer_init(&sync_timer, &timer_config, NULL);
	if (err != NRFX_SUCCESS) {
		return -EIO;
	}

	if (!nrfx_gpiote_init_check(&sync_gpiote)) {
		return -ENODEV;
	}
	err = nrfx_gpiote_channel_alloc(&sync_gpiote, &sync_gpiote_channel);
	if (err != NRFX_SUCCESS) {
		return -ENOMEM;
	}
	err = nrfx_gpiote_input_configure(&sync_gpiote, sync_pin, &input_config);
	if (err != NRFX_SUCCESS) {
		return -EIO;
	}
	err = nrfx_gppi_channel_alloc(&sync_gppi_channel);
	if (err != NRFX_SUCCESS) {
		return -ENOMEM;
	}

	nrfx_gppi_channel_endpoints_setup(
		sync_gppi_channel,
		nrfx_gpiote_in_event_address_get(&sync_gpiote, sync_pin),
		nrfx_timer_capture_task_address_get(&sync_timer,
						    NRF_TIMER_CC_CHANNEL1));
	nrfx_gppi_channels_enable(BIT(sync_gppi_channel));
	nrfx_timer_enable(&sync_timer);
	nrfx_gpiote_trigger_enable(&sync_gpiote, sync_pin, true);
	return 0;
}
#endif

int timebase_init(void)
{
	k_spinlock_key_t key = k_spin_lock(&capture_lock);

	last_sync_capture = 0u;
#if defined(CONFIG_RF_LINK_SYNC_HW_CAPTURE)
	last_timer_raw = 0u;
	timer_epoch = 0u;
#endif
	k_spin_unlock(&capture_lock, key);
#if defined(CONFIG_RF_LINK_SYNC_HW_CAPTURE)
	return hardware_timebase_init();
#else
	return 0;
#endif
}

uint64_t timebase_now_ticks(void)
{
#if defined(CONFIG_RF_LINK_SYNC_HW_CAPTURE)
	return extend_timer_value(
		nrfx_timer_capture(&sync_timer, NRF_TIMER_CC_CHANNEL0));
#else
	return k_cycle_get_64();
#endif
}

uint32_t timebase_frequency_hz(void)
{
#if defined(CONFIG_RF_LINK_SYNC_HW_CAPTURE)
	return NRFX_MHZ_TO_HZ(1);
#else
	return (uint32_t)sys_clock_hw_cycles_per_sec();
#endif
}

void timebase_on_sync_capture(uint64_t captured_tick)
{
	k_spinlock_key_t key = k_spin_lock(&capture_lock);

	last_sync_capture = captured_tick;
	k_spin_unlock(&capture_lock, key);
}

uint64_t timebase_last_sync_capture(void)
{
	k_spinlock_key_t key = k_spin_lock(&capture_lock);
	uint64_t captured_tick = last_sync_capture;

	k_spin_unlock(&capture_lock, key);
	return captured_tick;
}
