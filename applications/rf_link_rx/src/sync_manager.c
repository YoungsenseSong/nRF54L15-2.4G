#include "sync_manager.h"

#include <string.h>
#include <zephyr/spinlock.h>

#include "timebase.h"

static struct k_spinlock sync_lock;
static struct sync_status sync_status_data;
static uint64_t arm_tick;
static bool have_armed_once;

int sync_manager_init(void)
{
	k_spinlock_key_t key = k_spin_lock(&sync_lock);

	memset(&sync_status_data, 0, sizeof(sync_status_data));
	sync_status_data.state = SYNC_IDLE;
	arm_tick = 0u;
	have_armed_once = false;
	k_spin_unlock(&sync_lock, key);
	return 0;
}

int sync_manager_arm(uint32_t epoch)
{
	k_spinlock_key_t key = k_spin_lock(&sync_lock);

	if (have_armed_once) {
		sync_status_data.resync_count++;
	}
	have_armed_once = true;
	sync_status_data.state = SYNC_ARMED;
	sync_status_data.sync_epoch = epoch;
	sync_status_data.sync_tick = 0u;
	sync_status_data.start_sample_index = 0u;
	sync_status_data.sync_locked = false;
	arm_tick = timebase_now_ticks();
	k_spin_unlock(&sync_lock, key);
	return 0;
}

void sync_manager_on_sync_capture(uint64_t tick)
{
	k_spinlock_key_t key = k_spin_lock(&sync_lock);

	if (sync_status_data.state != SYNC_ARMED) {
		k_spin_unlock(&sync_lock, key);
		return;
	}

	timebase_on_sync_capture(tick);
	sync_status_data.sync_tick = tick;
	sync_status_data.sync_capture_count++;
	sync_status_data.state = SYNC_WAIT_START;
	k_spin_unlock(&sync_lock, key);
}

void sync_manager_on_frame(struct rx_frame_record *record)
{
	k_spinlock_key_t key;

	if (record == NULL) {
		return;
	}

	key = k_spin_lock(&sync_lock);
	record->sync_epoch = sync_status_data.sync_epoch;

	if (sync_status_data.state == SYNC_WAIT_START &&
	    (record->status_flags & RX_FRAME_STATUS_BATCH_START) != 0u) {
		sync_status_data.state = SYNC_ALIGNING;
		sync_status_data.start_sample_index = record->logical_sample_index;
		record->logical_sample_index = 0u;
		record->status_flags |= RX_FRAME_STATUS_SYNCED;
		sync_status_data.sync_locked = true;
		sync_status_data.state = SYNC_LOCKED;
	} else if (sync_status_data.state == SYNC_LOCKED) {
		if (record->logical_sample_index >=
		    sync_status_data.start_sample_index) {
			record->logical_sample_index -=
				sync_status_data.start_sample_index;
			record->status_flags |= RX_FRAME_STATUS_SYNCED;
		} else {
			sync_status_data.state = SYNC_ERROR;
			sync_status_data.sync_locked = false;
		}
	} else if (sync_status_data.state == SYNC_DEGRADED) {
		record->status_flags |= RX_FRAME_STATUS_SYNC_DEGRADED;
	}

	k_spin_unlock(&sync_lock, key);
}

void sync_manager_on_sequence_gap(uint32_t lost_frames)
{
	if (lost_frames >= CONFIG_RF_LINK_SYNC_LARGE_GAP_FRAMES) {
		k_spinlock_key_t key = k_spin_lock(&sync_lock);

		sync_status_data.state = SYNC_DEGRADED;
		sync_status_data.sync_locked = false;
		k_spin_unlock(&sync_lock, key);
	}
}

void sync_manager_on_queue_overflow(void)
{
	k_spinlock_key_t key = k_spin_lock(&sync_lock);

	sync_status_data.state = SYNC_DEGRADED;
	sync_status_data.sync_locked = false;
	k_spin_unlock(&sync_lock, key);
}

void sync_manager_poll(uint64_t now_tick)
{
	uint64_t elapsed_ticks;
	uint64_t elapsed_ms;
	uint32_t frequency;
	k_spinlock_key_t key = k_spin_lock(&sync_lock);

	if (sync_status_data.state != SYNC_ARMED &&
	    sync_status_data.state != SYNC_WAIT_START) {
		k_spin_unlock(&sync_lock, key);
		return;
	}

	elapsed_ticks = now_tick - arm_tick;
	frequency = timebase_frequency_hz();
	elapsed_ms = (frequency == 0u) ? UINT64_MAX :
		(elapsed_ticks * 1000u) / frequency;
	if (elapsed_ms >= CONFIG_RF_LINK_SYNC_TIMEOUT_MS) {
		sync_status_data.sync_timeout++;
		sync_status_data.sync_locked = false;
		sync_status_data.state = SYNC_DEGRADED;
	}
	k_spin_unlock(&sync_lock, key);
}

void sync_manager_stop(void)
{
	k_spinlock_key_t key = k_spin_lock(&sync_lock);

	sync_status_data.state = SYNC_IDLE;
	sync_status_data.sync_locked = false;
	k_spin_unlock(&sync_lock, key);
}

void sync_manager_get_status(struct sync_status *status)
{
	k_spinlock_key_t key;

	if (status == NULL) {
		return;
	}

	key = k_spin_lock(&sync_lock);
	*status = sync_status_data;
	k_spin_unlock(&sync_lock, key);
}
