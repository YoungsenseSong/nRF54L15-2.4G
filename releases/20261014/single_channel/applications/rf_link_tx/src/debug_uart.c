#include "debug_uart.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/ring_buffer.h>
#include <string.h>

#define DEBUG_UART_NODE DT_NODELABEL(uart00)
#define DEBUG_UART_FIFO_SIZE 4096U
#define DEBUG_UART_DMA_CHUNK_SIZE 256U

static const struct device *const uart_dev = DEVICE_DT_GET_OR_NULL(DEBUG_UART_NODE);
RING_BUF_DECLARE(debug_uart_fifo, DEBUG_UART_FIFO_SIZE);
static uint8_t dma_tx_buffer[DEBUG_UART_DMA_CHUNK_SIZE];
static struct k_spinlock state_lock;
static bool ready;
static bool tx_active;

static void debug_uart_kick(void);

static void debug_uart_callback(const struct device *dev,
					struct uart_event *evt,
					void *user_data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(user_data);

	if (evt == NULL) {
		return;
	}

	if (evt->type != UART_TX_DONE && evt->type != UART_TX_ABORTED) {
		return;
	}

	k_spinlock_key_t key = k_spin_lock(&state_lock);
	tx_active = false;
	k_spin_unlock(&state_lock, key);

	/* The callback runs in interrupt context; uart_tx() is safe here. */
	debug_uart_kick();
}

static void debug_uart_kick(void)
{
	uint32_t length;
	k_spinlock_key_t key = k_spin_lock(&state_lock);

	if (!ready || tx_active || ring_buf_size_get(&debug_uart_fifo) == 0U) {
		k_spin_unlock(&state_lock, key);
		return;
	}

	length = ring_buf_get(&debug_uart_fifo, dma_tx_buffer,
				     sizeof(dma_tx_buffer));
	if (length == 0U) {
		k_spin_unlock(&state_lock, key);
		return;
	}

	/* Keep the DMA buffer reserved until UART_TX_DONE/UART_TX_ABORTED. */
	tx_active = true;
	k_spin_unlock(&state_lock, key);

	if (uart_tx(uart_dev, dma_tx_buffer, length, SYS_FOREVER_MS) != 0) {
		key = k_spin_lock(&state_lock);
		tx_active = false;
		k_spin_unlock(&state_lock, key);

		/* Keep the driver state consistent; the failed chunk is dropped. */
	}
}

static void debug_uart_enqueue(const uint8_t *data, size_t length)
{
	k_spinlock_key_t key;

	if (!ready || data == NULL || length == 0U) {
		return;
	}

	key = k_spin_lock(&state_lock);
	(void)ring_buf_put(&debug_uart_fifo, data, (uint32_t)length);
	k_spin_unlock(&state_lock, key);

	debug_uart_kick();
}

bool debug_uart_init(void)
{
	int ret;

	ready = (uart_dev != NULL) && device_is_ready(uart_dev);
	if (!ready) {
		return false;
	}

	ret = uart_callback_set(uart_dev, debug_uart_callback, NULL);
	if (ret != 0) {
		ready = false;
		return false;
	}

	ring_buf_reset(&debug_uart_fifo);
	tx_active = false;
	return ready;
}

bool debug_uart_ready(void)
{
	return ready;
}

void debug_uart_putc(char c)
{
	uint8_t byte = (uint8_t)c;

	debug_uart_enqueue(&byte, sizeof(byte));
}

void debug_uart_puts(const char *s)
{
	debug_uart_enqueue((const uint8_t *)s, (s == NULL) ? 0U : strlen(s));
}

void debug_uart_u32(uint32_t value)
{
	char buf[11];
	int pos = (int)sizeof(buf) - 1;

	buf[pos] = '\0';
	do {
		pos--;
		buf[pos] = (char)('0' + (value % 10u));
		value /= 10u;
	} while (value != 0u);

	debug_uart_puts(&buf[pos]);
}

void debug_uart_i32(int32_t value)
{
	if (value < 0) {
		debug_uart_putc('-');
		debug_uart_u32((uint32_t)(-value));
	} else {
		debug_uart_u32((uint32_t)value);
	}
}

void debug_uart_crlf(void)
{
	debug_uart_puts("\r\n");
}
