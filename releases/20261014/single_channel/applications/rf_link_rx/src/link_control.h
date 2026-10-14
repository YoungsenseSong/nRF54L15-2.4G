#ifndef RF_LINK_LINK_CONTROL_H_
#define RF_LINK_LINK_CONTROL_H_

#include <stdbool.h>
#include <stdint.h>

#include "fpga_spi_transport.h"

struct link_control_status {
	bool streaming;
	uint32_t arm_commands;
	uint32_t start_commands;
	uint32_t stop_commands;
	uint32_t reset_commands;
	uint32_t invalid_commands;
};

void link_control_init(void);
int link_control_handle_command(enum fpga_command_code command,
				uint32_t argument, void *context);
void link_control_get_status(struct link_control_status *status);
bool link_control_streaming(void);

#endif /* RF_LINK_LINK_CONTROL_H_ */
