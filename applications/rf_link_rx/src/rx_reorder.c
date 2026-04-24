#include "rx_reorder.h"

#include <errno.h>
#include <zephyr/sys/atomic.h>

#include "rx_sample_stream.h"

static atomic_t frames;
static atomic_t samples;
static atomic_t bytes;
static atomic_t lost_frames;
static atomic_t duplicates;
static atomic_t bad_magic;
static atomic_t bad_size;

static uint16_t expected_seq;
static uint16_t last_seq;
static int16_t last_first_sample;
static int16_t last_last_sample;
static bool have_seq;

void rx_reorder_init(void)
{
	atomic_clear(&frames);
	atomic_clear(&samples);
	atomic_clear(&bytes);
	atomic_clear(&lost_frames);
	atomic_clear(&duplicates);
	atomic_clear(&bad_magic);
	atomic_clear(&bad_size);
	expected_seq = 0;
	last_seq = 0;
	last_first_sample = 0;
	last_last_sample = 0;
	have_seq = false;
}

int rx_reorder_process_frame(const struct rf_frame *frame, uint8_t len)
{
	uint16_t gap;

	if (frame == NULL || len != RF_LINK_FRAME_WIRE_SIZE) {
		atomic_inc(&bad_size);
		return -EINVAL;
	}

	if (frame->magic != RF_LINK_MAGIC ||
	    frame->sample_count == 0u ||
	    frame->sample_count > RF_LINK_FRAME_SAMPLE_COUNT) {
		atomic_inc(&bad_magic);
		return -EBADMSG;
	}

	if (!have_seq) {
		have_seq = true;
		expected_seq = (uint16_t)(frame->seq + 1u);
	} else if (frame->seq == expected_seq) {
		expected_seq = (uint16_t)(expected_seq + 1u);
	} else {
		gap = (uint16_t)(frame->seq - expected_seq);
		if (gap < 0x8000u) {
			atomic_add(&lost_frames, gap);
			expected_seq = (uint16_t)(frame->seq + 1u);
		} else {
			atomic_inc(&duplicates);
		}
	}

	last_seq = frame->seq;
	last_first_sample = frame->samples[0];
	last_last_sample = frame->samples[frame->sample_count - 1u];

	atomic_inc(&frames);
	atomic_add(&samples, frame->sample_count);
	atomic_add(&bytes, frame->sample_count * sizeof(frame->samples[0]));
	rx_sample_stream_submit_frame(frame);

	return 0;
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
	stats->duplicates = (uint32_t)atomic_get(&duplicates);
	stats->bad_magic = (uint32_t)atomic_get(&bad_magic);
	stats->bad_size = (uint32_t)atomic_get(&bad_size);
	stats->last_seq = last_seq;
	stats->last_first_sample = last_first_sample;
	stats->last_last_sample = last_last_sample;
}
