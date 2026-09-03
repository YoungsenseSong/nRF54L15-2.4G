#ifndef RF_LINK_FPGA_TRANSPORT_H_
#define RF_LINK_FPGA_TRANSPORT_H_

#include <stdbool.h>
#include <stdint.h>

#include "fpga_spi_transport.h"
#include "rf_link_future.h"

struct transport_stats {
	uint32_t records;
	uint32_t crc_errors;
	uint32_t invalid_cmd;
	uint32_t duplicate_commit;
	uint32_t submit_errors;
	uint32_t stall_ms;
	uint32_t request_transactions;
	uint32_t response_transactions;
	uint32_t spi_errors;
	uint32_t parser_errors;
	uint32_t short_transfers;
};

struct fpga_transport_api {
	int (*init)(void);
	bool (*ready)(void);
	int (*submit)(const struct fpga_record *record);
	int (*get_command)(struct fpga_command *command);
	void (*get_stats)(struct transport_stats *stats);
};

int fpga_transport_init(void);
int fpga_transport_service(void);
int fpga_transport_build_record(const struct rx_frame_record *source,
				uint32_t transport_seq,
				struct fpga_record *record);
bool fpga_transport_record_valid(const struct fpga_record *record);
void fpga_transport_get_stats(struct transport_stats *stats);

#endif /* RF_LINK_FPGA_TRANSPORT_H_ */
