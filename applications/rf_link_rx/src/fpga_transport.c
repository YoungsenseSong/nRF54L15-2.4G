#include "fpga_transport.h"

#include <errno.h>
#include <stddef.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/spinlock.h>
#include <zephyr/sys/crc.h>

#include "frame_queue.h"
#include "link_control.h"
#include "timebase.h"

#if defined(CONFIG_RF_LINK_FPGA_SPIS) && CONFIG_RF_LINK_FPGA_SPIS
#include "fpga_spis_backend.h"
#endif

static struct k_spinlock transport_lock;
K_MUTEX_DEFINE(transport_service_mutex);
static struct transport_stats transport_stats_data;
static uint32_t next_transport_seq;
static uint32_t pending_extended_frame_seq;
static uint64_t pending_since_tick;
static bool transport_initialized;

static uint16_t header_crc(const struct fpga_record_header *header)
{
	return crc16(0x1021u, 0xffffu, (const uint8_t *)header,
		     offsetof(struct fpga_record_header, header_crc16));
}

int fpga_transport_build_record(const struct rx_frame_record *source,
				uint32_t transport_seq,
				struct fpga_record *record)
{
	if (source == NULL || record == NULL) {
		return -EINVAL;
	}

	memset(record, 0, sizeof(*record));
	record->header.magic = FPGA_RECORD_MAGIC;
	record->header.version = FPGA_RECORD_VERSION;
	record->header.node_id = source->node_id;
	record->header.record_type = 1u;
	record->header.transport_seq = transport_seq;
	record->header.sync_epoch = source->sync_epoch;
	record->header.rx_tick = source->rx_tick;
	record->header.logical_sample_index = source->logical_sample_index;
	record->header.status_flags = source->status_flags;
	record->header.payload_len = RF_LINK_FRAME_WIRE_SIZE;
	record->header.header_crc16 = header_crc(&record->header);
	record->frame = source->frame;
	record->payload_crc32 =
		crc32_ieee((const uint8_t *)&record->frame, sizeof(record->frame));
	return 0;
}

bool fpga_transport_record_valid(const struct fpga_record *record)
{
	return record != NULL &&
	       record->header.magic == FPGA_RECORD_MAGIC &&
	       record->header.version == FPGA_RECORD_VERSION &&
	       record->header.payload_len == RF_LINK_FRAME_WIRE_SIZE &&
	       record->header.header_crc16 == header_crc(&record->header) &&
	       record->payload_crc32 ==
		crc32_ieee((const uint8_t *)&record->frame, sizeof(record->frame));
}

static int release_record(uint32_t extended_frame_seq, bool drop, void *context)
{
	int ret;

	ARG_UNUSED(drop);
	ARG_UNUSED(context);
	ret = frame_queue_commit(extended_frame_seq);
	if (ret == 0) {
		k_spinlock_key_t key = k_spin_lock(&transport_lock);

		next_transport_seq++;
		pending_since_tick = 0u;
		k_spin_unlock(&transport_lock, key);
	}
	return ret;
}

static int transport_control_command(enum fpga_command_code command,
				     uint32_t argument, void *context)
{
	k_spinlock_key_t key;

	if (command == FPGA_CMD_ARM_SYNC || command == FPGA_CMD_RESET_LINK) {
		fpga_spi_transport_abort_pending();
		key = k_spin_lock(&transport_lock);
		pending_since_tick = 0u;
		k_spin_unlock(&transport_lock, key);
	}

	if (command == FPGA_CMD_CLEAR_STATS) {
		key = k_spin_lock(&transport_lock);
		memset(&transport_stats_data, 0, sizeof(transport_stats_data));
		k_spin_unlock(&transport_lock, key);
	}

	return link_control_handle_command(command, argument, context);
}

static int debug_backend_init(void)
{
	return 0;
}

static bool debug_backend_ready(void)
{
	return true;
}

static int debug_backend_submit(const struct fpga_record *record)
{
	return fpga_transport_record_valid(record) ? 0 : -EBADMSG;
}

static int debug_backend_get_command(struct fpga_command *command)
{
	ARG_UNUSED(command);
	return -EAGAIN;
}

static void debug_backend_get_stats(struct transport_stats *stats)
{
	ARG_UNUSED(stats);
}

static const struct fpga_transport_api debug_backend = {
	.init = debug_backend_init,
	.ready = debug_backend_ready,
	.submit = debug_backend_submit,
	.get_command = debug_backend_get_command,
	.get_stats = debug_backend_get_stats,
};

static int spis_backend_init(void)
{
#if defined(CONFIG_RF_LINK_FPGA_SPIS) && CONFIG_RF_LINK_FPGA_SPIS
	return fpga_spis_backend_init();
#else
	return -ENOTSUP;
#endif
}

static bool spis_backend_ready(void)
{
#if defined(CONFIG_RF_LINK_FPGA_SPIS) && CONFIG_RF_LINK_FPGA_SPIS
	return fpga_spis_backend_ready();
#else
	return false;
#endif
}

static int spis_backend_submit(const struct fpga_record *record)
{
#if defined(CONFIG_RF_LINK_FPGA_SPIS) && CONFIG_RF_LINK_FPGA_SPIS
	return fpga_spis_backend_submit(record, pending_extended_frame_seq);
#else
	return fpga_spi_transport_stage(record, pending_extended_frame_seq);
#endif
}

static int spis_backend_get_command(struct fpga_command *command)
{
	ARG_UNUSED(command);
	return -EAGAIN;
}

static void spis_backend_get_stats(struct transport_stats *stats)
{
	struct fpga_spi_transport_stats spi_stats;
#if defined(CONFIG_RF_LINK_FPGA_SPIS) && CONFIG_RF_LINK_FPGA_SPIS
	struct fpga_spis_backend_stats physical_stats;
#endif

	if (stats == NULL) {
		return;
	}

	fpga_spi_transport_stats_get(&spi_stats);
	stats->crc_errors += spi_stats.crc_errors;
	stats->invalid_cmd += spi_stats.invalid_cmd;
	stats->duplicate_commit += spi_stats.duplicate_commit;
#if defined(CONFIG_RF_LINK_FPGA_SPIS) && CONFIG_RF_LINK_FPGA_SPIS
	fpga_spis_backend_get_stats(&physical_stats);
	stats->request_transactions += physical_stats.request_transactions;
	stats->response_transactions += physical_stats.response_transactions;
	stats->spi_errors += physical_stats.spi_errors;
	stats->parser_errors += physical_stats.parser_errors;
	stats->short_transfers += physical_stats.short_transfers;
#endif
}

static const struct fpga_transport_api spis_backend = {
	.init = spis_backend_init,
	.ready = spis_backend_ready,
	.submit = spis_backend_submit,
	.get_command = spis_backend_get_command,
	.get_stats = spis_backend_get_stats,
};

static const struct fpga_transport_api *selected_backend(void)
{
	if (IS_ENABLED(CONFIG_RF_LINK_FPGA_SPIS)) {
		return &spis_backend;
	}

	return &debug_backend;
}

int fpga_transport_init(void)
{
	const struct fpga_transport_api *backend = selected_backend();
	int ret;

	memset(&transport_stats_data, 0, sizeof(transport_stats_data));
	next_transport_seq = 0u;
	pending_extended_frame_seq = 0u;
	pending_since_tick = 0u;
	transport_initialized = false;

	ret = fpga_spi_transport_init(release_record,
				      transport_control_command, NULL);
	if (ret != 0) {
		return ret;
	}

	ret = backend->init();
	if (ret != 0) {
		return ret;
	}

	transport_initialized = true;
	return 0;
}

int fpga_transport_service(void)
{
	const struct fpga_transport_api *backend = selected_backend();
	struct rx_frame_record source;
	struct fpga_record record;
	int processed = 0;
	int result;
	int ret;

	ret = k_mutex_lock(&transport_service_mutex, K_FOREVER);
	if (ret != 0) {
		return ret;
	}

	if (!transport_initialized || !backend->ready()) {
		result = -EAGAIN;
		goto out_unlock;
	}

	while (frame_queue_peek(&source) == 0) {
		if (IS_ENABLED(CONFIG_RF_LINK_FPGA_SPIS) &&
		    fpga_spi_transport_pending()) {
			break;
		}

		pending_extended_frame_seq = source.extended_frame_seq;
		ret = fpga_transport_build_record(&source, next_transport_seq,
						  &record);
		if (ret != 0) {
			result = ret;
			goto out_unlock;
		}

		ret = backend->submit(&record);
		if (ret != 0) {
			k_spinlock_key_t key = k_spin_lock(&transport_lock);

			transport_stats_data.submit_errors++;
			if (ret == -EBADMSG) {
				transport_stats_data.crc_errors++;
			}
			k_spin_unlock(&transport_lock, key);
			result = ret;
			goto out_unlock;
		}

		{
			k_spinlock_key_t key = k_spin_lock(&transport_lock);

			transport_stats_data.records++;
			if (pending_since_tick == 0u) {
				pending_since_tick = timebase_now_ticks();
			}
			k_spin_unlock(&transport_lock, key);
		}
		processed++;

		if (IS_ENABLED(CONFIG_RF_LINK_FPGA_SPIS)) {
			break;
		}

		ret = release_record(source.extended_frame_seq, false, NULL);
		if (ret != 0) {
			result = ret;
			goto out_unlock;
		}
	}

	result = processed;

out_unlock:
	k_mutex_unlock(&transport_service_mutex);
	return result;
}

void fpga_transport_get_stats(struct transport_stats *stats)
{
	const struct fpga_transport_api *backend = selected_backend();
	uint64_t elapsed_ticks;
	uint32_t frequency;
	k_spinlock_key_t key;

	if (stats == NULL) {
		return;
	}

	key = k_spin_lock(&transport_lock);
	*stats = transport_stats_data;
	if (pending_since_tick != 0u) {
		elapsed_ticks = timebase_now_ticks() - pending_since_tick;
		frequency = timebase_frequency_hz();
		if (frequency != 0u) {
			stats->stall_ms = (uint32_t)MIN(
				(elapsed_ticks * 1000u) / frequency, UINT32_MAX);
		}
	}
	k_spin_unlock(&transport_lock, key);
	backend->get_stats(stats);
}
