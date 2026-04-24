#include "rx_sample_stream.h"

#include <errno.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

#define RX_SAMPLE_STREAM_UART_NODE DT_NODELABEL(uart30)

BUILD_ASSERT(sizeof(struct rx_sample_stream_record) == 224u,
	     "rx_sample_stream_record size must stay fixed");

#if IS_ENABLED(CONFIG_RF_LINK_RX_SAMPLE_STREAM)

static const struct device *const uart_dev =
	DEVICE_DT_GET_OR_NULL(RX_SAMPLE_STREAM_UART_NODE);
K_MSGQ_DEFINE(stream_msgq, sizeof(struct rx_sample_stream_record),
	      CONFIG_RF_LINK_RX_SAMPLE_STREAM_BUFFERED_RECORDS, 4);
static K_THREAD_STACK_DEFINE(stream_thread_stack,
			     CONFIG_RF_LINK_RX_SAMPLE_STREAM_THREAD_STACK_SIZE);
static struct k_thread stream_thread;
static atomic_t stream_drop_total;
static bool stream_active;
static uint32_t stream_frame_index;

static void uart_write_buf(const uint8_t *buf, size_t len)
{
	size_t i;

	if (buf == NULL || !stream_active) {
		return;
	}

	for (i = 0; i < len; i++) {
		uart_poll_out(uart_dev, buf[i]);
	}
}

static void stream_thread_entry(void *arg1, void *arg2, void *arg3)
{
	struct rx_sample_stream_record record;

	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	while (1) {
		(void)k_msgq_get(&stream_msgq, &record, K_FOREVER);
		uart_write_buf((const uint8_t *)&record, sizeof(record));
	}
}

int rx_sample_stream_init(void)
{
	struct uart_config cfg = {
		.baudrate = CONFIG_RF_LINK_RX_SAMPLE_STREAM_UART_BAUD,
		.parity = UART_CFG_PARITY_NONE,
		.stop_bits = UART_CFG_STOP_BITS_1,
		.data_bits = UART_CFG_DATA_BITS_8,
		.flow_ctrl = UART_CFG_FLOW_CTRL_NONE,
	};
	int ret;

	if (stream_active) {
		return 0;
	}

	if (uart_dev == NULL || !device_is_ready(uart_dev)) {
		return -ENODEV;
	}

	ret = uart_configure(uart_dev, &cfg);
	if (ret != 0) {
		return ret;
	}

	atomic_clear(&stream_drop_total);
	stream_frame_index = 0u;
	stream_active = true;

	k_thread_create(&stream_thread, stream_thread_stack,
			K_THREAD_STACK_SIZEOF(stream_thread_stack),
			stream_thread_entry, NULL, NULL, NULL,
			K_PRIO_PREEMPT(7), 0, K_NO_WAIT);
	k_thread_name_set(&stream_thread, "rx_sample_stream");

	return 0;
}

bool rx_sample_stream_active(void)
{
	return stream_active;
}

void rx_sample_stream_submit_frame(const struct rf_frame *frame)
{
	struct rx_sample_stream_record record;
	int ret;

	if (!stream_active || frame == NULL) {
		return;
	}

	record.magic = RF_LINK_RX_STREAM_MAGIC;
	record.record_len = (uint16_t)sizeof(record);
	record.version = RF_LINK_RX_STREAM_VERSION;
	record.reserved = 0u;
	record.stream_frame_index = stream_frame_index++;
	record.stream_drop_total = (uint32_t)atomic_get(&stream_drop_total);
	record.stream_uptime_ms = k_uptime_get_32();
	memcpy(&record.frame, frame, sizeof(record.frame));

	ret = k_msgq_put(&stream_msgq, &record, K_NO_WAIT);
	if (ret != 0) {
		atomic_inc(&stream_drop_total);
	}
}

#else

int rx_sample_stream_init(void)
{
	return 0;
}

bool rx_sample_stream_active(void)
{
	return false;
}

void rx_sample_stream_submit_frame(const struct rf_frame *frame)
{
	ARG_UNUSED(frame);
}

#endif
