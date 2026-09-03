#ifndef RF_LINK_FPGA_SPIS_BACKEND_H_
#define RF_LINK_FPGA_SPIS_BACKEND_H_

#include <stdbool.h>
#include <stdint.h>

#include "rf_link_future.h"

struct fpga_spis_backend_stats {
	uint32_t request_transactions;
	uint32_t response_transactions;
	uint32_t spi_errors;
	uint32_t parser_errors;
	uint32_t short_transfers;
};

int fpga_spis_backend_init(void);
bool fpga_spis_backend_ready(void);
int fpga_spis_backend_submit(const struct fpga_record *record,
			     uint32_t extended_frame_seq);
void fpga_spis_backend_get_stats(struct fpga_spis_backend_stats *stats);

#endif /* RF_LINK_FPGA_SPIS_BACKEND_H_ */
