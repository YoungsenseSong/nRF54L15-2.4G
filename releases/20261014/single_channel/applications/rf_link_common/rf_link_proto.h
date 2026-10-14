#ifndef RF_LINK_PROTO_H_
#define RF_LINK_PROTO_H_

#include <stdint.h>
#include <zephyr/sys/util.h>

#define RF_LINK_MAGIC                    0xA55Au
#define RF_LINK_FRAME_SAMPLE_COUNT       96u
#define RF_LINK_FRAME_FLAGS_TEST         BIT(0)
#define RF_LINK_FRAME_FLAGS_MEMS         BIT(1)
#define RF_LINK_FRAME_FLAGS_BATCH_START  BIT(2)
#define RF_LINK_FRAME_FLAGS_BATCH_END    BIT(3)
#define RF_LINK_CHANNEL                  40u
#define RF_LINK_PIPE                     0u
#define RF_LINK_PIPE_PREFIX              0x54u
#define RF_LINK_MEMS_AXIS_COUNT          3u
#define RF_LINK_MEMS_ODR_HZ              4000u
#define RF_LINK_TARGET_SAMPLE_HZ         (RF_LINK_MEMS_AXIS_COUNT * RF_LINK_MEMS_ODR_HZ)
#define RF_LINK_STATUS_PERIOD_MS         1000u
#define RF_LINK_NOACK_STREAM             1u

struct rf_frame {
	uint16_t magic;
	uint16_t seq;
	uint16_t sample_count;
	uint16_t flags;
	uint32_t timestamp_ms;
	int16_t samples[RF_LINK_FRAME_SAMPLE_COUNT];
} __packed;

#define RF_LINK_FRAME_WIRE_SIZE sizeof(struct rf_frame)

BUILD_ASSERT(RF_LINK_FRAME_WIRE_SIZE == 204u,
	     "rf_frame wire size must stay fixed for this RF protocol revision");

#endif /* RF_LINK_PROTO_H_ */
