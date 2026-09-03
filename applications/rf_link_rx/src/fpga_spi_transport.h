#ifndef RF_LINK_FPGA_SPI_TRANSPORT_H_
#define RF_LINK_FPGA_SPI_TRANSPORT_H_

#include <stdbool.h>
#include <stdint.h>

#include "rf_link_future.h"

enum fpga_command_code {
	FPGA_CMD_GET_INFO = 1,
	FPGA_CMD_GET_STATUS,
	FPGA_CMD_PEEK_RECORD,
	FPGA_CMD_READ_RECORD,
	FPGA_CMD_COMMIT_RECORD,
	FPGA_CMD_DROP_RECORD,
	FPGA_CMD_CLEAR_STATS,
	FPGA_CMD_ARM_SYNC,
	FPGA_CMD_START_STREAM,
	FPGA_CMD_STOP_STREAM,
	FPGA_CMD_RESET_LINK,
};

#define FPGA_INFO_MAGIC                 0x3149464eu
#define FPGA_STATUS_MAGIC               0x3153464eu
#define FPGA_SPI_CONTRACT_VERSION       1u
#define FPGA_SPI_BIT_ORDER_MSB_FIRST    0u
#define FPGA_CAP_PEEK_COMMIT            BIT(0)
#define FPGA_CAP_LEVEL_DRDY             BIT(1)
#define FPGA_CAP_HW_SYNC_CAPTURE        BIT(2)

struct fpga_info_payload {
	uint32_t magic;
	uint16_t contract_version;
	uint16_t capabilities;
	uint16_t request_size;
	uint16_t response_size;
	uint16_t record_size;
	uint8_t spi_mode;
	uint8_t bit_order;
	uint32_t max_sclk_hz;
	uint32_t initial_sclk_hz;
	uint32_t min_request_response_gap_us;
} __packed;

struct fpga_status_payload {
	uint32_t magic;
	uint16_t contract_version;
	uint16_t sync_state;
	uint32_t queue_level;
	uint32_t queue_high_water;
	uint32_t sync_epoch;
	uint64_t sync_tick;
	uint32_t pending_transport_seq;
	uint32_t crc_errors;
	uint32_t invalid_cmd;
	uint32_t duplicate_commit;
	uint32_t spi_errors;
	uint32_t parser_errors;
	uint32_t short_transfers;
} __packed;

struct fpga_command {
	uint8_t code;
	uint8_t reserved[3];
	uint32_t argument;
} __packed;

struct fpga_response {
	int32_t status;
	uint32_t transport_seq;
	uint8_t record_valid;
	uint8_t reserved[3];
	union {
		struct fpga_record record;
		uint8_t payload[sizeof(struct fpga_record)];
	};
} __packed;

/*
 * The command engine's current wire image is little-endian on nRF54L15.
 * A physical SPIS backend may add turnaround/dummy clocks, but it must not
 * silently change these request/response payload sizes.
 */
#define FPGA_SPI_REQUEST_SIZE  8u
#define FPGA_SPI_RESPONSE_SIZE 260u

BUILD_ASSERT(sizeof(struct fpga_command) == FPGA_SPI_REQUEST_SIZE,
	     "fpga_command wire size changed");
BUILD_ASSERT(sizeof(struct fpga_response) == FPGA_SPI_RESPONSE_SIZE,
	     "fpga_response wire size changed");
BUILD_ASSERT(sizeof(struct fpga_info_payload) == 28u,
	     "GET_INFO wire payload size changed");
BUILD_ASSERT(sizeof(struct fpga_status_payload) == 56u,
	     "GET_STATUS wire payload size changed");
BUILD_ASSERT(sizeof(struct fpga_status_payload) <= sizeof(struct fpga_record),
	     "status payload is too large for the response envelope");

struct fpga_spi_transport_stats {
	uint32_t crc_errors;
	uint32_t invalid_cmd;
	uint32_t duplicate_commit;
	uint32_t read_total;
	uint32_t commit_total;
	uint32_t drop_total;
};

typedef int (*fpga_spi_release_cb_t)(uint32_t extended_frame_seq,
				      bool drop, void *context);
typedef int (*fpga_spi_control_cb_t)(enum fpga_command_code command,
				      uint32_t argument, void *context);

int fpga_spi_transport_init(fpga_spi_release_cb_t release_cb,
			    fpga_spi_control_cb_t control_cb,
			    void *context);
int fpga_spi_transport_stage(const struct fpga_record *record,
			     uint32_t extended_frame_seq);
int fpga_spi_transport_execute(const struct fpga_command *command,
			       struct fpga_response *response);
bool fpga_spi_transport_pending(void);
uint32_t fpga_spi_transport_pending_seq(void);
void fpga_spi_transport_abort_pending(void);
void fpga_spi_transport_stats_get(struct fpga_spi_transport_stats *stats);

#endif /* RF_LINK_FPGA_SPI_TRANSPORT_H_ */
