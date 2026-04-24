#ifndef RF_LINK_RX_SAMPLE_STREAM_H_
#define RF_LINK_RX_SAMPLE_STREAM_H_

#include <stdbool.h>
#include <stdint.h>

#include "proto.h"

#define RF_LINK_RX_STREAM_MAGIC   0x31535852u
#define RF_LINK_RX_STREAM_VERSION 1u

struct rx_sample_stream_record {
	uint32_t magic;
	uint16_t record_len;
	uint8_t version;
	uint8_t reserved;
	uint32_t stream_frame_index;
	uint32_t stream_drop_total;
	uint32_t stream_uptime_ms;
	struct rf_frame frame;
} __packed;

int rx_sample_stream_init(void);
bool rx_sample_stream_active(void);
void rx_sample_stream_submit_frame(const struct rf_frame *frame);

#endif /* RF_LINK_RX_SAMPLE_STREAM_H_ */
