#ifndef RF_LINK_FUTURE_H_
#define RF_LINK_FUTURE_H_

#include <stdint.h>
#include <zephyr/sys/util.h>

#include "rf_link_proto.h"

#define RF_LINK_PROTOCOL_VERSION_V1 1u
#define FPGA_RECORD_MAGIC            0x3146524eu
#define FPGA_RECORD_VERSION          1u

#define RX_FRAME_STATUS_VALID            BIT(0)
#define RX_FRAME_STATUS_GAP_BEFORE       BIT(1)
#define RX_FRAME_STATUS_INDEX_ESTIMATED  BIT(2)
#define RX_FRAME_STATUS_BATCH_START      BIT(3)
#define RX_FRAME_STATUS_BATCH_END        BIT(4)
#define RX_FRAME_STATUS_SYNCED           BIT(5)
#define RX_FRAME_STATUS_SYNC_DEGRADED    BIT(6)

struct rx_missing_range {
	uint32_t first_extended_frame_seq;
	uint32_t frame_count;
	uint64_t first_logical_sample_index;
	uint64_t estimated_sample_count;
};

struct rx_frame_record {
	uint8_t node_id;
	uint8_t protocol_version;
	uint16_t reserved;
	uint32_t extended_frame_seq;
	uint32_t sync_epoch;
	uint64_t rx_tick;
	uint64_t logical_sample_index;
	uint32_t status_flags;
	struct rf_frame frame;
};

struct fpga_record_header {
	uint32_t magic;
	uint8_t version;
	uint8_t node_id;
	uint16_t record_type;
	uint32_t transport_seq;
	uint32_t sync_epoch;
	uint64_t rx_tick;
	uint64_t logical_sample_index;
	uint32_t status_flags;
	uint16_t payload_len;
	uint16_t header_crc16;
} __packed;

struct fpga_record {
	struct fpga_record_header header;
	struct rf_frame frame;
	uint32_t payload_crc32;
} __packed;

BUILD_ASSERT(sizeof(struct rx_frame_record) == 240u,
	     "rx_frame_record layout changed");
BUILD_ASSERT(sizeof(struct fpga_record_header) == 40u,
	     "fpga_record_header layout changed");
BUILD_ASSERT(sizeof(struct fpga_record) == 248u,
	     "fpga_record layout changed");

#endif /* RF_LINK_FUTURE_H_ */
