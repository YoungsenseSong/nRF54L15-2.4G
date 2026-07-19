#include "rx_reorder.h"

#include <errno.h>
#include <string.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/spinlock.h>

#include "rx_sample_stream.h"
#include "timebase.h"

static atomic_t frames;
static atomic_t samples;
static atomic_t bytes;
static atomic_t lost_frames;
static atomic_t lost_samples;
static atomic_t duplicates;
static atomic_t late_frames;
static atomic_t index_estimated;
static atomic_t bad_magic;
static atomic_t bad_size;
static atomic_t bad_sample_count;

static struct k_spinlock state_lock;
static uint16_t last_seq;
static uint32_t extended_frame_seq;
static uint64_t local_sample_index;
static struct rx_missing_range last_missing;
static int16_t last_first_sample;
static int16_t last_last_sample;
static bool have_seq;

void rx_reorder_init(void)
{
	atomic_clear(&frames);
	atomic_clear(&samples);
	atomic_clear(&bytes);
	atomic_clear(&lost_frames);
	atomic_clear(&lost_samples);
	atomic_clear(&duplicates);
	atomic_clear(&late_frames);
	atomic_clear(&index_estimated);
	atomic_clear(&bad_magic);
	atomic_clear(&bad_size);
	atomic_clear(&bad_sample_count);
	extended_frame_seq = 0u;
	local_sample_index = 0u;
	memset(&last_missing, 0, sizeof(last_missing));
	last_seq = 0;
	last_first_sample = 0;
	last_last_sample = 0;
	have_seq = false;
}

int rx_reorder_process_packet(const struct rf_frame *frame, uint16_t len,
			      uint64_t rx_tick,
			      struct rx_frame_record *record)
{
	k_spinlock_key_t key;
	uint16_t expected_seq;
	uint16_t forward_gap;
	uint32_t estimated_samples = 0u;
	uint32_t status_flags = RX_FRAME_STATUS_VALID;

	if (frame == NULL || record == NULL || len != RF_LINK_FRAME_WIRE_SIZE) {
		atomic_inc(&bad_size);
		return -EINVAL;
	}

	if (frame->magic != RF_LINK_MAGIC) {
		atomic_inc(&bad_magic);
		return -EBADMSG;
	}
	if (frame->sample_count == 0u ||
	    frame->sample_count > RF_LINK_FRAME_SAMPLE_COUNT) {
		atomic_inc(&bad_sample_count);
		return -EMSGSIZE;
	}

	key = k_spin_lock(&state_lock);
	if (!have_seq) {
		have_seq = true;
		extended_frame_seq = frame->seq;
	} else {
		expected_seq = (uint16_t)(last_seq + 1u);
		forward_gap = (uint16_t)(frame->seq - expected_seq);
		if (forward_gap == 0u) {
			extended_frame_seq++;
		} else if (forward_gap < 0x8000u) {
			estimated_samples = (uint32_t)forward_gap *
				RF_LINK_FRAME_SAMPLE_COUNT;
			last_missing.first_extended_frame_seq =
				extended_frame_seq + 1u;
			last_missing.frame_count = forward_gap;
			last_missing.first_logical_sample_index = local_sample_index;
			last_missing.estimated_sample_count = estimated_samples;
			extended_frame_seq += (uint32_t)forward_gap + 1u;
			local_sample_index += estimated_samples;
			atomic_add(&lost_frames, forward_gap);
			atomic_add(&lost_samples, estimated_samples);
			atomic_inc(&index_estimated);
			status_flags |= RX_FRAME_STATUS_GAP_BEFORE |
					RX_FRAME_STATUS_INDEX_ESTIMATED;
		} else {
			if (frame->seq == last_seq) {
				atomic_inc(&duplicates);
				k_spin_unlock(&state_lock, key);
				return -EALREADY;
			}

			atomic_inc(&late_frames);
			k_spin_unlock(&state_lock, key);
			return -ERANGE;
		}
	}

	memset(record, 0, sizeof(*record));
#if defined(CONFIG_RF_LINK_FUTURE_MODE)
	record->node_id = CONFIG_RF_LINK_NODE_ID;
#else
	record->node_id = 0u;
#endif
	record->protocol_version = RF_LINK_PROTOCOL_VERSION_V1;
	record->extended_frame_seq = extended_frame_seq;
	record->rx_tick = rx_tick;
	record->logical_sample_index = local_sample_index;
	record->status_flags = status_flags;
	if ((frame->flags & RF_LINK_FRAME_FLAGS_BATCH_START) != 0u) {
		record->status_flags |= RX_FRAME_STATUS_BATCH_START;
	}
	if ((frame->flags & RF_LINK_FRAME_FLAGS_BATCH_END) != 0u) {
		record->status_flags |= RX_FRAME_STATUS_BATCH_END;
	}
	memcpy(&record->frame, frame, sizeof(record->frame));

	last_seq = frame->seq;
	last_first_sample = frame->samples[0];
	last_last_sample = frame->samples[frame->sample_count - 1u];
	local_sample_index += frame->sample_count;
	k_spin_unlock(&state_lock, key);

	atomic_inc(&frames);
	atomic_add(&samples, frame->sample_count);
	atomic_add(&bytes, frame->sample_count * sizeof(frame->samples[0]));

	return 0;
}

int rx_reorder_process_frame(const struct rf_frame *frame, uint8_t len)
{
	struct rx_frame_record record;
	int ret;

	ret = rx_reorder_process_packet(frame, len, timebase_now_ticks(), &record);
	if (ret == 0) {
		rx_sample_stream_submit_frame(frame);
	}

	return ret;
}

void rx_reorder_stats_get(struct rx_reorder_stats *stats)
{
	if (stats == NULL) {
		return;
	}

	stats->frames = (uint32_t)atomic_get(&frames);
	stats->samples = (uint32_t)atomic_get(&samples);
	stats->bytes = (uint32_t)atomic_get(&bytes);
	stats->lost_frames = (uint32_t)atomic_get(&lost_frames);
	stats->lost_samples = (uint32_t)atomic_get(&lost_samples);
	stats->duplicates = (uint32_t)atomic_get(&duplicates);
	stats->late_frames = (uint32_t)atomic_get(&late_frames);
	stats->index_estimated = (uint32_t)atomic_get(&index_estimated);
	stats->bad_magic = (uint32_t)atomic_get(&bad_magic);
	stats->bad_size = (uint32_t)atomic_get(&bad_size);
	stats->bad_sample_count = (uint32_t)atomic_get(&bad_sample_count);
	{
		k_spinlock_key_t key = k_spin_lock(&state_lock);

		stats->extended_frame_seq = extended_frame_seq;
		stats->last_seq = last_seq;
		stats->last_first_sample = last_first_sample;
		stats->last_last_sample = last_last_sample;
		stats->last_missing = last_missing;
		k_spin_unlock(&state_lock, key);
	}
}
