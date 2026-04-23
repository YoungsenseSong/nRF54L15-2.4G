#include <zephyr/kernel.h>

#include "adc_sampler.h"
#include "ipc_tx.h"
#include "proto.h"

int main(void)
{
	struct rf_frame frame;
	int ret;

	adc_sampler_init();

	ret = ipc_tx_init();
	if (ret != 0) {
		return ret;
	}

	ret = ipc_tx_wait_bound(K_FOREVER);
	if (ret != 0) {
		return ret;
	}

	while (1) {
		adc_sampler_fill_frame(&frame);
		(void)ipc_tx_send_frame(&frame, K_USEC(200));

		k_sleep(K_USEC(RF_LINK_TX_PERIOD_US));
	}
}
