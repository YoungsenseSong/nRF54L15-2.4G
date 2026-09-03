#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <zephyr/sys/crc.h>
#include <zephyr/ztest.h>

#include "fpga_spi_transport.h"
#include "fpga_transport.h"
#include "frame_queue.h"
#include "link_control.h"
#include "rf_link_proto.h"
#include "rx_reorder.h"
#include "sync_manager.h"

static uint64_t fake_tick;
static uint64_t captured_tick;

int timebase_init(void)
{
	fake_tick = 0u;
	captured_tick = 0u;
	return 0;
}

uint64_t timebase_now_ticks(void)
{
	return fake_tick;
}

uint32_t timebase_frequency_hz(void)
{
	return 1000000u;
}

void timebase_on_sync_capture(uint64_t tick)
{
	captured_tick = tick;
}

uint64_t timebase_last_sync_capture(void)
{
	return captured_tick;
}

void rx_sample_stream_submit_frame(const struct rf_frame *frame)
{
	ARG_UNUSED(frame);
}

static struct rf_frame make_frame(uint16_t seq, uint16_t sample_count,
				  uint16_t flags)
{
	struct rf_frame frame = {0};

	frame.magic = RF_LINK_MAGIC;
	frame.seq = seq;
	frame.sample_count = sample_count;
	frame.flags = flags;
	for (uint16_t i = 0u; i < sample_count; i++) {
		frame.samples[i] = (int16_t)(seq + i);
	}
	return frame;
}

static struct rx_frame_record make_record(uint32_t extended_seq)
{
	struct rx_frame_record record = {0};

	record.node_id = 0u;
	record.protocol_version = RF_LINK_PROTOCOL_VERSION_V1;
	record.extended_frame_seq = extended_seq;
	record.sync_epoch = 7u;
	record.rx_tick = 1234u;
	record.logical_sample_index = 4096u;
	record.status_flags = RX_FRAME_STATUS_VALID;
	record.frame = make_frame((uint16_t)extended_seq,
				  RF_LINK_FRAME_SAMPLE_COUNT, 0u);
	return record;
}

static void future_before(void *fixture)
{
	ARG_UNUSED(fixture);
	fake_tick = 0u;
	captured_tick = 0u;
	rx_reorder_init();
	frame_queue_init();
	link_control_init();
	zassert_ok(sync_manager_init());
}

ZTEST(rf_link_future_c, test_crc16_ccitt_false_reference_vector)
{
	static const uint8_t check[] = "123456789";

	zassert_equal(crc16(0x1021u, 0xffffu, check, sizeof(check) - 1u),
		      0x29b1u);
}

ZTEST(rf_link_future_c, test_wire_layout_and_record_crc)
{
	struct rx_frame_record source = make_record(17u);
	struct fpga_record record;

	zassert_equal(sizeof(struct fpga_command), FPGA_SPI_REQUEST_SIZE);
	zassert_equal(sizeof(struct fpga_response), FPGA_SPI_RESPONSE_SIZE);
	zassert_equal(sizeof(struct fpga_record), 248u);
	zassert_equal(sizeof(struct fpga_info_payload), 28u);
	zassert_equal(sizeof(struct fpga_status_payload), 56u);
	zassert_equal(offsetof(struct fpga_response, payload), 12u);
	zassert_ok(fpga_transport_build_record(&source, 3u, &record));
	zassert_true(fpga_transport_record_valid(&record));
	zassert_equal(record.header.transport_seq, 3u);
	zassert_equal(record.header.payload_len, RF_LINK_FRAME_WIRE_SIZE);
	zassert_equal(crc16(0x1021u, 0xffffu,
			    (const uint8_t *)&record.header,
			    offsetof(struct fpga_record_header,
				     header_crc16)),
		      record.header.header_crc16);

	record.frame.samples[10] ^= 1;
	zassert_false(fpga_transport_record_valid(&record));
	record.frame.samples[10] ^= 1;
	record.header.node_id ^= 1u;
	zassert_false(fpga_transport_record_valid(&record));
}

ZTEST(rf_link_future_c, test_actual_reorder_wrap_gap_duplicate_and_late)
{
	struct rx_reorder_stats stats;
	struct rx_frame_record record;
	struct rf_frame frame = make_frame(0xfffeu, 96u, 0u);

	zassert_ok(rx_reorder_process_packet(&frame, sizeof(frame), 1u, &record));
	frame = make_frame(0xffffu, 96u, 0u);
	zassert_ok(rx_reorder_process_packet(&frame, sizeof(frame), 2u, &record));
	frame = make_frame(0u, 96u, 0u);
	zassert_ok(rx_reorder_process_packet(&frame, sizeof(frame), 3u, &record));
	zassert_equal(record.extended_frame_seq, 0x10000u);

	zassert_equal(rx_reorder_process_packet(&frame, sizeof(frame), 4u, &record),
		      -EALREADY);
	frame = make_frame(0xffffu, 96u, 0u);
	zassert_equal(rx_reorder_process_packet(&frame, sizeof(frame), 5u, &record),
		      -ERANGE);
	frame = make_frame(2u, 64u, RF_LINK_FRAME_FLAGS_BATCH_END);
	zassert_ok(rx_reorder_process_packet(&frame, sizeof(frame), 6u, &record));
	zassert_true((record.status_flags & RX_FRAME_STATUS_GAP_BEFORE) != 0u);
	zassert_true((record.status_flags & RX_FRAME_STATUS_INDEX_ESTIMATED) != 0u);
	zassert_equal(record.logical_sample_index, 384u);

	rx_reorder_stats_get(&stats);
	zassert_equal(stats.lost_frames, 1u);
	zassert_equal(stats.lost_samples, 96u);
	zassert_equal(stats.duplicates, 1u);
	zassert_equal(stats.late_frames, 1u);
}

ZTEST(rf_link_future_c, test_queue_64_peek_commit_and_overflow)
{
	struct frame_queue_stats stats;
	struct rx_frame_record record;
	struct rx_frame_record peeked;

	for (uint32_t i = 0u; i < 64u; i++) {
		record = make_record(i);
		zassert_ok(frame_queue_push(&record));
	}
	record = make_record(64u);
	zassert_equal(frame_queue_push(&record), -ENOSPC);
	zassert_equal(frame_queue_level(), 64u);
	zassert_ok(frame_queue_peek(&peeked));
	zassert_equal(peeked.extended_frame_seq, 0u);
	zassert_equal(frame_queue_commit(1u), -ESTALE);
	zassert_equal(frame_queue_level(), 64u);
	zassert_ok(frame_queue_commit(0u));
	zassert_equal(frame_queue_level(), 63u);

	frame_queue_stats_get(&stats);
	zassert_equal(stats.high_water, 64u);
	zassert_equal(stats.overflow, 1u);
}

ZTEST(rf_link_future_c, test_sync_first_batch_start_maps_to_zero)
{
	struct rx_frame_record record = make_record(100u);
	struct sync_status status;

	fake_tick = 10u;
	zassert_ok(sync_manager_arm(12u));
	sync_manager_on_sync_capture(100u);
	record.logical_sample_index = 8192u;
	record.status_flags |= RX_FRAME_STATUS_BATCH_START;
	sync_manager_on_frame(&record);
	zassert_equal(record.sync_epoch, 12u);
	zassert_equal(record.logical_sample_index, 0u);
	zassert_true((record.status_flags & RX_FRAME_STATUS_SYNCED) != 0u);

	record.logical_sample_index = 8288u;
	record.status_flags = RX_FRAME_STATUS_VALID;
	sync_manager_on_frame(&record);
	zassert_equal(record.logical_sample_index, 96u);
	sync_manager_get_status(&status);
	zassert_equal(status.state, SYNC_LOCKED);
	zassert_true(status.sync_locked);
}

ZTEST(rf_link_future_c, test_debug_backend_auto_commit_actual_c_path)
{
	struct transport_stats stats;
	struct rx_frame_record source = make_record(21u);

	zassert_ok(fpga_transport_init());
	zassert_ok(frame_queue_push(&source));
	zassert_equal(fpga_transport_service(), 1);
	zassert_equal(frame_queue_level(), 0u);
	fpga_transport_get_stats(&stats);
	zassert_equal(stats.records, 1u);
	zassert_equal(stats.crc_errors, 0u);
	zassert_equal(stats.submit_errors, 0u);
}

ZTEST(rf_link_future_c, test_actual_peek_commit_and_error_counters)
{
	struct fpga_spi_transport_stats stats;
	struct rx_frame_record source = make_record(55u);
	struct fpga_command command = {0};
	struct fpga_response first;
	struct fpga_response second;
	struct fpga_record record;

	zassert_ok(fpga_transport_init());
	zassert_ok(frame_queue_push(&source));
	zassert_ok(fpga_transport_build_record(&source, 9u, &record));
	zassert_ok(fpga_spi_transport_stage(&record, source.extended_frame_seq));

	command.code = FPGA_CMD_PEEK_RECORD;
	zassert_ok(fpga_spi_transport_execute(&command, &first));
	zassert_ok(fpga_spi_transport_execute(&command, &second));
	zassert_true(first.record_valid);
	zassert_mem_equal(&first.record, &second.record, sizeof(first.record));
	zassert_equal(frame_queue_level(), 1u);

	command.code = FPGA_CMD_COMMIT_RECORD;
	command.argument = 10u;
	zassert_equal(fpga_spi_transport_execute(&command, &first), -ESTALE);
	zassert_equal(frame_queue_level(), 1u);
	command.argument = 9u;
	zassert_ok(fpga_spi_transport_execute(&command, &first));
	zassert_equal(frame_queue_level(), 0u);
	zassert_equal(fpga_spi_transport_execute(&command, &first), -EALREADY);

	command.code = 0xffu;
	zassert_equal(fpga_spi_transport_execute(&command, &first), -ENOTSUP);
	fpga_spi_transport_stats_get(&stats);
	zassert_equal(stats.read_total, 2u);
	zassert_equal(stats.commit_total, 1u);
	zassert_equal(stats.duplicate_commit, 1u);
	zassert_equal(stats.invalid_cmd, 2u);

	record.payload_crc32 ^= 1u;
	zassert_equal(fpga_spi_transport_stage(&record, 56u), -EBADMSG);
	fpga_spi_transport_stats_get(&stats);
	zassert_equal(stats.crc_errors, 1u);
}

ZTEST_SUITE(rf_link_future_c, NULL, NULL, future_before, NULL, NULL);
