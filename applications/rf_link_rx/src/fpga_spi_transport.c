#include "fpga_spi_transport.h"

#include <errno.h>
#include <stddef.h>
#include <string.h>
#include <zephyr/spinlock.h>
#include <zephyr/sys/crc.h>

static struct k_spinlock spi_lock;
static struct fpga_record pending_record;
static struct fpga_spi_transport_stats spi_stats;
static fpga_spi_release_cb_t release_callback;
static fpga_spi_control_cb_t control_callback;
static void *callback_context;
static uint32_t pending_extended_frame_seq;
static uint32_t last_committed_transport_seq;
static bool have_last_commit;
static bool have_pending_record;
static bool commit_in_progress;
static bool abort_requested;

static uint16_t record_header_crc(const struct fpga_record_header *header)
{
	return crc16_ccitt(0xffffu, (const uint8_t *)header,
			 offsetof(struct fpga_record_header, header_crc16));
}

static bool record_crc_valid(const struct fpga_record *record)
{
	return record != NULL &&
	       record->header.magic == FPGA_RECORD_MAGIC &&
	       record->header.version == FPGA_RECORD_VERSION &&
	       record->header.payload_len == RF_LINK_FRAME_WIRE_SIZE &&
	       record->header.header_crc16 == record_header_crc(&record->header) &&
	       record->payload_crc32 ==
		crc32_ieee((const uint8_t *)&record->frame, sizeof(record->frame));
}

int fpga_spi_transport_init(fpga_spi_release_cb_t release_cb,
			    fpga_spi_control_cb_t control_cb,
			    void *context)
{
	k_spinlock_key_t key;

	if (release_cb == NULL) {
		return -EINVAL;
	}

	key = k_spin_lock(&spi_lock);
	memset(&pending_record, 0, sizeof(pending_record));
	memset(&spi_stats, 0, sizeof(spi_stats));
	release_callback = release_cb;
	control_callback = control_cb;
	callback_context = context;
	pending_extended_frame_seq = 0u;
	last_committed_transport_seq = 0u;
	have_last_commit = false;
	have_pending_record = false;
	commit_in_progress = false;
	abort_requested = false;
	k_spin_unlock(&spi_lock, key);
	return 0;
}

int fpga_spi_transport_stage(const struct fpga_record *record,
			     uint32_t extended_frame_seq)
{
	k_spinlock_key_t key;

	if (!record_crc_valid(record)) {
		key = k_spin_lock(&spi_lock);
		spi_stats.crc_errors++;
		k_spin_unlock(&spi_lock, key);
		return -EBADMSG;
	}

	key = k_spin_lock(&spi_lock);
	if (have_pending_record) {
		k_spin_unlock(&spi_lock, key);
		return -EBUSY;
	}

	pending_record = *record;
	pending_extended_frame_seq = extended_frame_seq;
	have_pending_record = true;
	k_spin_unlock(&spi_lock, key);
	return 0;
}

static int commit_or_drop(uint32_t transport_seq, bool drop)
{
	uint32_t extended_frame_seq;
	int ret;
	k_spinlock_key_t key = k_spin_lock(&spi_lock);

	if (!have_pending_record) {
		if (have_last_commit && transport_seq == last_committed_transport_seq) {
			spi_stats.duplicate_commit++;
			k_spin_unlock(&spi_lock, key);
			return -EALREADY;
		}

		spi_stats.invalid_cmd++;
		k_spin_unlock(&spi_lock, key);
		return -ENODATA;
	}
	if (commit_in_progress) {
		k_spin_unlock(&spi_lock, key);
		return -EBUSY;
	}

	if (transport_seq != pending_record.header.transport_seq) {
		spi_stats.invalid_cmd++;
		k_spin_unlock(&spi_lock, key);
		return -ESTALE;
	}

	extended_frame_seq = pending_extended_frame_seq;
	commit_in_progress = true;
	k_spin_unlock(&spi_lock, key);
	ret = release_callback(extended_frame_seq, drop, callback_context);
	key = k_spin_lock(&spi_lock);
	commit_in_progress = false;
	if (abort_requested) {
		abort_requested = false;
		have_pending_record = false;
		k_spin_unlock(&spi_lock, key);
		return (ret == 0) ? -ECANCELED : ret;
	}
	if (ret != 0) {
		k_spin_unlock(&spi_lock, key);
		return ret;
	}

	last_committed_transport_seq = transport_seq;
	have_last_commit = true;
	have_pending_record = false;
	if (drop) {
		spi_stats.drop_total++;
	} else {
		spi_stats.commit_total++;
	}
	k_spin_unlock(&spi_lock, key);
	return 0;
}

int fpga_spi_transport_execute(const struct fpga_command *command,
			       struct fpga_response *response)
{
	int ret = 0;
	k_spinlock_key_t key;

	if (command == NULL || response == NULL) {
		return -EINVAL;
	}

	memset(response, 0, sizeof(*response));
	switch ((enum fpga_command_code)command->code) {
	case FPGA_CMD_GET_INFO:
	case FPGA_CMD_GET_STATUS:
		break;
	case FPGA_CMD_PEEK_RECORD:
	case FPGA_CMD_READ_RECORD:
		key = k_spin_lock(&spi_lock);
		if (!have_pending_record) {
			ret = -ENODATA;
		} else {
			response->record = pending_record;
			response->record_valid = true;
			response->transport_seq =
				pending_record.header.transport_seq;
			spi_stats.read_total++;
		}
		k_spin_unlock(&spi_lock, key);
		break;
	case FPGA_CMD_COMMIT_RECORD:
		ret = commit_or_drop(command->argument, false);
		break;
	case FPGA_CMD_DROP_RECORD:
		ret = commit_or_drop(command->argument, true);
		break;
	case FPGA_CMD_CLEAR_STATS:
		key = k_spin_lock(&spi_lock);
		memset(&spi_stats, 0, sizeof(spi_stats));
		k_spin_unlock(&spi_lock, key);
		if (control_callback != NULL) {
			ret = control_callback(FPGA_CMD_CLEAR_STATS, 0u,
					       callback_context);
		}
		break;
	case FPGA_CMD_ARM_SYNC:
	case FPGA_CMD_START_STREAM:
	case FPGA_CMD_STOP_STREAM:
	case FPGA_CMD_RESET_LINK:
		if (control_callback == NULL) {
			ret = -ENOTSUP;
		} else {
			ret = control_callback(
				(enum fpga_command_code)command->code,
				command->argument, callback_context);
		}
		break;
	default:
		key = k_spin_lock(&spi_lock);
		spi_stats.invalid_cmd++;
		k_spin_unlock(&spi_lock, key);
		ret = -ENOTSUP;
		break;
	}

	response->status = ret;
	return ret;
}

bool fpga_spi_transport_pending(void)
{
	k_spinlock_key_t key = k_spin_lock(&spi_lock);
	bool pending = have_pending_record;

	k_spin_unlock(&spi_lock, key);
	return pending;
}

void fpga_spi_transport_abort_pending(void)
{
	k_spinlock_key_t key = k_spin_lock(&spi_lock);

	if (commit_in_progress) {
		abort_requested = true;
		k_spin_unlock(&spi_lock, key);
		return;
	}

	have_pending_record = false;
	pending_extended_frame_seq = 0u;
	commit_in_progress = false;
	abort_requested = false;
	k_spin_unlock(&spi_lock, key);
}

void fpga_spi_transport_stats_get(struct fpga_spi_transport_stats *stats)
{
	k_spinlock_key_t key;

	if (stats == NULL) {
		return;
	}

	key = k_spin_lock(&spi_lock);
	*stats = spi_stats;
	k_spin_unlock(&spi_lock, key);
}
