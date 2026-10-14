#ifndef RF_LINK_SYNC_MANAGER_H_
#define RF_LINK_SYNC_MANAGER_H_

#include <stdbool.h>
#include <stdint.h>

#include "rf_link_future.h"

enum sync_state {
	SYNC_IDLE,
	SYNC_ARMED,
	SYNC_WAIT_START,
	SYNC_ALIGNING,
	SYNC_LOCKED,
	SYNC_DEGRADED,
	SYNC_ERROR,
};

struct sync_status {
	enum sync_state state;
	uint32_t sync_capture_count;
	uint32_t sync_timeout;
	uint32_t sync_epoch;
	uint32_t resync_count;
	uint64_t sync_tick;
	uint64_t start_sample_index;
	bool sync_locked;
};

int sync_manager_init(void);
int sync_manager_arm(uint32_t epoch);
void sync_manager_on_sync_capture(uint64_t tick);
void sync_manager_on_frame(struct rx_frame_record *record);
void sync_manager_on_sequence_gap(uint32_t lost_frames);
void sync_manager_on_queue_overflow(void);
void sync_manager_poll(uint64_t now_tick);
void sync_manager_stop(void);
void sync_manager_get_status(struct sync_status *status);

#endif /* RF_LINK_SYNC_MANAGER_H_ */
