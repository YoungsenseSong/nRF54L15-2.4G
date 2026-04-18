#ifndef RF_LINK_ADC_SAMPLER_H_
#define RF_LINK_ADC_SAMPLER_H_

#include <stdint.h>

#include "proto.h"

void adc_sampler_init(void);
void adc_sampler_fill_frame(struct rf_frame *frame);

#endif /* RF_LINK_ADC_SAMPLER_H_ */
