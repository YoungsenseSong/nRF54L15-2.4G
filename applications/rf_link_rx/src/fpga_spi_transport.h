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

struct fpga_command {
	uint8_t code;
	uint8_t reserved[3];
	uint32_t argument;
};

struct fpga_response {
	int32_t status;
	uint32_t transport_seq;
	bool record_valid;
	struct fpga_record record;
};

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
void fpga_spi_transport_abort_pending(void);
void fpga_spi_transport_stats_get(struct fpga_spi_transport_stats *stats);

#endif /* RF_LINK_FPGA_SPI_TRANSPORT_H_ */
