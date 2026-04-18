#ifndef RF_LINK_PROTO_H_
#define RF_LINK_PROTO_H_

#include <stdint.h>
#include <zephyr/sys/util.h>

#define RF_LINK_MAGIC              0xA55Au
#define RF_LINK_FRAME_SAMPLE_COUNT 32u
#define RF_LINK_FRAME_FLAGS_TEST   BIT(0)
#define RF_LINK_CHANNEL            40u

struct rf_frame {
	uint16_t magic;
	uint16_t seq;
	uint16_t sample_count;
	uint16_t flags;
	uint32_t timestamp_ms;
	int16_t samples[RF_LINK_FRAME_SAMPLE_COUNT];
} __packed;

#define RF_LINK_FRAME_WIRE_SIZE sizeof(struct rf_frame)

BUILD_ASSERT(RF_LINK_FRAME_WIRE_SIZE == 76u,
	     "rf_frame wire size must stay fixed for the first RF protocol");

#endif /* RF_LINK_PROTO_H_ */
