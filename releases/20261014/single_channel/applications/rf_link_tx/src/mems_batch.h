#ifndef RF_LINK_MEMS_BATCH_H_
#define RF_LINK_MEMS_BATCH_H_

#include <stddef.h>
#include <stdint.h>
#include <zephyr/devicetree.h>
#include <zephyr/sys/util.h>

#include "rf_link_proto.h"

#define RF_LINK_MEMS_BATCH_MAGIC        0x4d424154u
#define RF_LINK_MEMS_BATCH_VERSION      1u
#define RF_LINK_MEMS_BATCH_SAMPLE_COUNT 4096u
#define RF_LINK_MEMS_BATCH_SLOT_COUNT   2u

#define RF_LINK_IPC_MAGIC               0x52464950u
#define RF_LINK_IPC_VERSION             1u

enum rf_link_ipc_message_type {
	RF_LINK_IPC_BATCH_READY = 1u,
	RF_LINK_IPC_BATCH_RELEASE = 2u,
};

struct rf_link_mems_batch {
	uint32_t magic;
	uint32_t version;
	uint32_t slot_index;
	uint32_t batch_seq;
	uint32_t sample_count;
	uint32_t capture_start_ms;
	uint32_t capture_end_ms;
	uint32_t sample_crc32;
	int16_t samples[RF_LINK_MEMS_BATCH_SAMPLE_COUNT];
} __aligned(32);

struct rf_link_ipc_message {
	uint32_t magic;
	uint32_t version;
	uint32_t type;
	uint32_t slot_index;
	uint32_t batch_seq;
	uint32_t sample_count;
	uint32_t sample_crc32;
};

#define RF_LINK_MEMS_SLOT0_NODE DT_NODELABEL(rf_link_mems_slot0)
#define RF_LINK_MEMS_SLOT1_NODE DT_NODELABEL(rf_link_mems_slot1)

#if !DT_NODE_HAS_STATUS(RF_LINK_MEMS_SLOT0_NODE, okay)
#error "rf_link_mems_slot0 is not enabled in devicetree"
#endif

#if !DT_NODE_HAS_STATUS(RF_LINK_MEMS_SLOT1_NODE, okay)
#error "rf_link_mems_slot1 is not enabled in devicetree"
#endif

BUILD_ASSERT(RF_LINK_MEMS_BATCH_SAMPLE_COUNT > RF_LINK_FRAME_SAMPLE_COUNT,
	     "MEMS batch must contain more than one radio frame");
BUILD_ASSERT(DT_REG_SIZE(RF_LINK_MEMS_SLOT0_NODE) >= sizeof(struct rf_link_mems_batch),
	     "MEMS shared slot 0 is too small");
BUILD_ASSERT(DT_REG_SIZE(RF_LINK_MEMS_SLOT1_NODE) >= sizeof(struct rf_link_mems_batch),
	     "MEMS shared slot 1 is too small");
BUILD_ASSERT((DT_REG_ADDR(RF_LINK_MEMS_SLOT0_NODE) % 32u) == 0u,
	     "MEMS shared slot 0 must be cache-line aligned");
BUILD_ASSERT((DT_REG_ADDR(RF_LINK_MEMS_SLOT1_NODE) % 32u) == 0u,
	     "MEMS shared slot 1 must be cache-line aligned");
BUILD_ASSERT(sizeof(struct rf_link_ipc_message) <= CONFIG_PBUF_RX_READ_BUF_SIZE,
	     "PBUF RX buffer is too small for the batch IPC descriptor");

static inline volatile struct rf_link_mems_batch *rf_link_mems_batch_slot(uint32_t index)
{
	uintptr_t address;

	if (index == 0u) {
		address = DT_REG_ADDR(RF_LINK_MEMS_SLOT0_NODE);
	} else if (index == 1u) {
		address = DT_REG_ADDR(RF_LINK_MEMS_SLOT1_NODE);
	} else {
		return NULL;
	}

	return (volatile struct rf_link_mems_batch *)address;
}

#endif /* RF_LINK_MEMS_BATCH_H_ */
