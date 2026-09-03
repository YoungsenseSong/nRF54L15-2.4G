#include "fpga_spis_backend.h"

#include <errno.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/spinlock.h>
#include <zephyr/sys/util.h>

#include "fpga_spi_transport.h"
#include "fpga_transport.h"
#include "frame_queue.h"
#include "sync_manager.h"

#define CH0_SPIS_NODE DT_NODELABEL(spi00)
#define CH0_FPGA_IF_NODE DT_NODELABEL(ch0_fpga_if)

BUILD_ASSERT(DT_NODE_HAS_STATUS(CH0_SPIS_NODE, okay),
	     "CH0 overlay must enable spi00");
BUILD_ASSERT(DT_NODE_HAS_PROP(CH0_FPGA_IF_NODE, drdy_gpios),
	     "CH0 overlay must define drdy-gpios");

static const struct device *const spis = DEVICE_DT_GET(CH0_SPIS_NODE);
static const struct gpio_dt_spec drdy =
	GPIO_DT_SPEC_GET(CH0_FPGA_IF_NODE, drdy_gpios);
static const struct spi_config spis_config = {
	.frequency = 1000000u,
	.operation = SPI_OP_MODE_SLAVE | SPI_WORD_SET(8),
	.slave = 0u,
};

static uint8_t request_tx[FPGA_SPI_REQUEST_SIZE];
static uint8_t request_rx[FPGA_SPI_REQUEST_SIZE];
static uint8_t response_rx[FPGA_SPI_RESPONSE_SIZE];
static struct fpga_response response_tx;
static struct k_thread spis_thread;
K_THREAD_STACK_DEFINE(spis_stack,
		      CONFIG_RF_LINK_FPGA_SPIS_THREAD_STACK_SIZE);
static struct k_spinlock backend_lock;
static struct fpga_spis_backend_stats backend_stats;
static bool backend_ready;

static void backend_stat_inc(uint32_t *counter)
{
	k_spinlock_key_t key = k_spin_lock(&backend_lock);

	(*counter)++;
	k_spin_unlock(&backend_lock, key);
}

static void update_drdy(void)
{
	(void)gpio_pin_set_dt(&drdy, fpga_spi_transport_pending() ? 1 : 0);
}

static void fill_info_response(struct fpga_response *response)
{
	struct fpga_info_payload info = {
		.magic = FPGA_INFO_MAGIC,
		.contract_version = FPGA_SPI_CONTRACT_VERSION,
		.capabilities = FPGA_CAP_PEEK_COMMIT | FPGA_CAP_LEVEL_DRDY |
				FPGA_CAP_HW_SYNC_CAPTURE,
		.request_size = FPGA_SPI_REQUEST_SIZE,
		.response_size = FPGA_SPI_RESPONSE_SIZE,
		.record_size = sizeof(struct fpga_record),
		.spi_mode = 0u,
		.bit_order = FPGA_SPI_BIT_ORDER_MSB_FIRST,
		.max_sclk_hz = 8000000u,
		.initial_sclk_hz = 1000000u,
		.min_request_response_gap_us = 1000u,
	};

	memcpy(response->payload, &info, sizeof(info));
}

static void fill_status_response(struct fpga_response *response)
{
	struct fpga_status_payload status = {0};
	struct fpga_spi_transport_stats command_stats;
	struct fpga_spis_backend_stats physical_stats;
	struct frame_queue_stats queue_stats;
	struct sync_status sync_status;

	fpga_spi_transport_stats_get(&command_stats);
	fpga_spis_backend_get_stats(&physical_stats);
	frame_queue_stats_get(&queue_stats);
	sync_manager_get_status(&sync_status);
	status.magic = FPGA_STATUS_MAGIC;
	status.contract_version = FPGA_SPI_CONTRACT_VERSION;
	status.sync_state = (uint16_t)sync_status.state;
	status.queue_level = frame_queue_level();
	status.queue_high_water = queue_stats.high_water;
	status.sync_epoch = sync_status.sync_epoch;
	status.sync_tick = sync_status.sync_tick;
	status.pending_transport_seq = fpga_spi_transport_pending_seq();
	status.crc_errors = command_stats.crc_errors;
	status.invalid_cmd = command_stats.invalid_cmd;
	status.duplicate_commit = command_stats.duplicate_commit;
	status.spi_errors = physical_stats.spi_errors;
	status.parser_errors = physical_stats.parser_errors;
	status.short_transfers = physical_stats.short_transfers;
	memcpy(response->payload, &status, sizeof(status));
}

static bool request_reserved_valid(const struct fpga_command *command)
{
	return command->reserved[0] == 0u && command->reserved[1] == 0u &&
	       command->reserved[2] == 0u;
}

static void spis_worker(void *arg1, void *arg2, void *arg3)
{
	const struct spi_buf request_tx_buf = {
		.buf = request_tx,
		.len = sizeof(request_tx),
	};
	const struct spi_buf request_rx_buf = {
		.buf = request_rx,
		.len = sizeof(request_rx),
	};
	const struct spi_buf_set request_tx_set = {
		.buffers = &request_tx_buf,
		.count = 1u,
	};
	const struct spi_buf_set request_rx_set = {
		.buffers = &request_rx_buf,
		.count = 1u,
	};
	const struct spi_buf response_tx_buf = {
		.buf = &response_tx,
		.len = sizeof(response_tx),
	};
	const struct spi_buf response_rx_buf = {
		.buf = response_rx,
		.len = sizeof(response_rx),
	};
	const struct spi_buf_set response_tx_set = {
		.buffers = &response_tx_buf,
		.count = 1u,
	};
	const struct spi_buf_set response_rx_set = {
		.buffers = &response_rx_buf,
		.count = 1u,
	};
	struct fpga_command command;
	int ret;

	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);
	while (true) {
		memset(request_rx, 0, sizeof(request_rx));
		ret = spi_transceive(spis, &spis_config, &request_tx_set,
				     &request_rx_set);
		if (ret < 0) {
			backend_stat_inc(&backend_stats.spi_errors);
			continue;
		}
		backend_stat_inc(&backend_stats.request_transactions);
		if (ret != FPGA_SPI_REQUEST_SIZE) {
			backend_stat_inc(&backend_stats.short_transfers);
			continue;
		}

		memcpy(&command, request_rx, sizeof(command));
		memset(&response_tx, 0, sizeof(response_tx));
		if (!request_reserved_valid(&command)) {
			backend_stat_inc(&backend_stats.parser_errors);
			response_tx.status = -EBADMSG;
		} else {
			ret = fpga_spi_transport_execute(&command, &response_tx);
			if ((command.code == FPGA_CMD_COMMIT_RECORD ||
			     command.code == FPGA_CMD_DROP_RECORD) && ret == 0) {
				(void)fpga_transport_service();
			}
			if (command.code == FPGA_CMD_GET_INFO && ret == 0) {
				fill_info_response(&response_tx);
			} else if (command.code == FPGA_CMD_GET_STATUS && ret == 0) {
				fill_status_response(&response_tx);
			}
		}
		update_drdy();

		memset(response_rx, 0, sizeof(response_rx));
		ret = spi_transceive(spis, &spis_config, &response_tx_set,
				     &response_rx_set);
		if (ret < 0) {
			backend_stat_inc(&backend_stats.spi_errors);
			continue;
		}
		backend_stat_inc(&backend_stats.response_transactions);
		if (ret != FPGA_SPI_RESPONSE_SIZE) {
			backend_stat_inc(&backend_stats.short_transfers);
		}
	}
}

int fpga_spis_backend_init(void)
{
	int ret;

	if (!device_is_ready(spis) || !gpio_is_ready_dt(&drdy)) {
		return -ENODEV;
	}
	memset(&backend_stats, 0, sizeof(backend_stats));
	ret = gpio_pin_configure_dt(&drdy, GPIO_OUTPUT_INACTIVE);
	if (ret != 0) {
		return ret;
	}
	backend_ready = true;
	k_thread_create(&spis_thread, spis_stack, K_THREAD_STACK_SIZEOF(spis_stack),
			spis_worker, NULL, NULL, NULL,
			CONFIG_RF_LINK_FPGA_SPIS_THREAD_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(&spis_thread, "ch0_spis");
	return 0;
}

bool fpga_spis_backend_ready(void)
{
	return backend_ready;
}

int fpga_spis_backend_submit(const struct fpga_record *record,
			     uint32_t extended_frame_seq)
{
	int ret = fpga_spi_transport_stage(record, extended_frame_seq);

	if (ret == 0) {
		update_drdy();
	}
	return ret;
}

void fpga_spis_backend_get_stats(struct fpga_spis_backend_stats *stats)
{
	k_spinlock_key_t key;

	if (stats == NULL) {
		return;
	}
	key = k_spin_lock(&backend_lock);
	*stats = backend_stats;
	k_spin_unlock(&backend_lock, key);
}
