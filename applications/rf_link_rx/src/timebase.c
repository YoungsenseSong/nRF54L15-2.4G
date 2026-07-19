#include "timebase.h"

#include <zephyr/kernel.h>
#include <zephyr/spinlock.h>
#include <zephyr/sys_clock.h>

#if defined(CONFIG_RF_LINK_SYNC_HW_CAPTURE)
#error "RF_LINK_SYNC_HW_CAPTURE requires a reviewed receiver-board overlay and hardware backend"
#endif

static struct k_spinlock capture_lock;
static uint64_t last_sync_capture;

int timebase_init(void)
{
	k_spinlock_key_t key = k_spin_lock(&capture_lock);

	last_sync_capture = 0u;
	k_spin_unlock(&capture_lock, key);
	return 0;
}

uint64_t timebase_now_ticks(void)
{
	return k_cycle_get_64();
}

uint32_t timebase_frequency_hz(void)
{
	return (uint32_t)sys_clock_hw_cycles_per_sec();
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
