#include <zephyr/fatal.h>

#include "lp_trace.h"

void k_sys_fatal_error_handler(unsigned int reason, const struct arch_esf *esf)
{
	ARG_UNUSED(esf);

	rf_link_lp_trace_note_fatal(reason);
	k_fatal_halt(reason);
}
