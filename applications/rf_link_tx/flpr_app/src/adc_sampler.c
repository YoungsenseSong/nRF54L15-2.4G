#include "adc_sampler.h"

#include <zephyr/kernel.h>

static uint16_t seq;
static int16_t sample_value;

void adc_sampler_init(void)
{
	seq = 0;
	sample_value = 0;
}

void adc_sampler_fill_frame(struct rf_frame *frame)
{
	if (frame == NULL) {
		return;
	}

	frame->magic = RF_LINK_MAGIC;
	frame->seq = seq++;
	frame->sample_count = RF_LINK_FRAME_SAMPLE_COUNT;
	frame->flags = RF_LINK_FRAME_FLAGS_TEST;
	frame->timestamp_ms = k_uptime_get_32();

	for (uint32_t i = 0; i < RF_LINK_FRAME_SAMPLE_COUNT; i++) {
		frame->samples[i] = sample_value;
		sample_value = (int16_t)((sample_value + 1) & 0x0fff);
	}
}
