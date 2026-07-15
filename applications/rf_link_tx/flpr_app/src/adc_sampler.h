#ifndef RF_LINK_ADC_SAMPLER_H_
#define RF_LINK_ADC_SAMPLER_H_

#include <stddef.h>
#include <stdint.h>

#include "rf_link_proto.h"

struct adc_sampler_stats {
	uint32_t irq_count;
	uint32_t poll_fallbacks;
	uint32_t fifo_reads;
	uint32_t fifo_packets;
	uint32_t samples_captured;
	uint32_t malformed_packets;
	uint32_t spi_errors;
	uint32_t fifo_flushes;
	uint32_t fifo_overflows;
	int32_t last_error;
};

int adc_sampler_init(void);
int adc_sampler_read_samples(int16_t *samples, size_t sample_count);
void adc_sampler_stats_get(struct adc_sampler_stats *stats);

#endif /* RF_LINK_ADC_SAMPLER_H_ */
