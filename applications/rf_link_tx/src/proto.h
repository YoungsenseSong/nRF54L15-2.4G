#ifndef RF_LINK_PROTO_H_
#define RF_LINK_PROTO_H_

#include <stdint.h>
#include <zephyr/sys/util.h>

#define RF_LINK_MAGIC              0xA55Au
#define RF_LINK_FRAME_SAMPLE_COUNT 96u
#define RF_LINK_FRAME_FLAGS_TEST   BIT(0)
#define RF_LINK_CHANNEL            40u
#define RF_LINK_TARGET_SAMPLE_HZ   50000u
#define RF_LINK_TX_PERIOD_US       ((RF_LINK_FRAME_SAMPLE_COUNT * 1000000u) / \
				    RF_LINK_TARGET_SAMPLE_HZ)
#define RF_LINK_STATUS_PERIOD_MS   1000u
#define RF_LINK_NOACK_STREAM       1u
#define RF_LINK_LP_START_DELAY_MS  0u
#define RF_LINK_LP_WARMUP_FRAMES   16u
#define RF_LINK_LP_WARMUP_PERIOD_MS 10u
#define RF_LINK_LP_IPC_SEND_TIMEOUT_MS 5u
#define RF_LINK_LP_USE_ABSOLUTE_PACING 1u

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
BUILD_ASSERT(CONFIG_PBUF_RX_READ_BUF_SIZE >= RF_LINK_FRAME_WIRE_SIZE,
	     "PBUF RX buffer must be large enough for rf_frame IPC payloads");
BUILD_ASSERT(((RF_LINK_FRAME_SAMPLE_COUNT * 1000000u) %
	      RF_LINK_TARGET_SAMPLE_HZ) == 0u,
	     "RF_LINK_TX_PERIOD_US must be an integer number of microseconds");

#endif /* RF_LINK_PROTO_H_ */
