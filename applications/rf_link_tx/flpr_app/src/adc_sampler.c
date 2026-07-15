#include "adc_sampler.h"

#include <errno.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

#define IIM42352_NODE DT_NODELABEL(mems_sensor)

#define IIM42352_REG_INT_CONFIG         0x14u
#define IIM42352_REG_FIFO_CONFIG        0x16u
#define IIM42352_REG_INT_STATUS         0x2du
#define IIM42352_REG_FIFO_COUNTH        0x2eu
#define IIM42352_REG_FIFO_COUNTL        0x2fu
#define IIM42352_REG_FIFO_DATA          0x30u
#define IIM42352_REG_SIGNAL_PATH_RESET  0x4bu
#define IIM42352_REG_INTF_CONFIG0       0x4cu
#define IIM42352_REG_PWR_MGMT0          0x4eu
#define IIM42352_REG_ACCEL_CONFIG0      0x50u
#define IIM42352_REG_FIFO_CONFIG1       0x5fu
#define IIM42352_REG_FIFO_CONFIG2       0x60u
#define IIM42352_REG_FIFO_CONFIG3       0x61u
#define IIM42352_REG_INT_CONFIG1        0x64u
#define IIM42352_REG_INT_SOURCE0        0x65u
#define IIM42352_REG_WHO_AM_I           0x75u

#define IIM42352_WHO_AM_I_VALUE         0x6du
#define IIM42352_SPI_READ                BIT(7)
#define IIM42352_FIFO_PACKET_SIZE        8u
#define IIM42352_FIFO_WATERMARK_PACKETS  128u
#define IIM42352_FIFO_WATERMARK_BYTES    \
	(IIM42352_FIFO_WATERMARK_PACKETS * IIM42352_FIFO_PACKET_SIZE)
#define IIM42352_FIFO_MAX_BYTES          2080u
#define IIM42352_FIFO_MAX_SAMPLES        \
	((IIM42352_FIFO_MAX_BYTES / IIM42352_FIFO_PACKET_SIZE) * RF_LINK_MEMS_AXIS_COUNT)
#define IIM42352_IRQ_WAIT_MS             100u

#define IIM42352_SPI_OPERATION \
	(SPI_WORD_SET(8) | SPI_MODE_CPOL | SPI_MODE_CPHA | SPI_TRANSFER_MSB)

static const struct spi_dt_spec mems_spi =
	SPI_DT_SPEC_GET(IIM42352_NODE, IIM42352_SPI_OPERATION, 0);
static const struct gpio_dt_spec mems_int1 =
	GPIO_DT_SPEC_GET(IIM42352_NODE, int1_gpios);

static K_SEM_DEFINE(fifo_irq_sem, 0, 1);
static struct gpio_callback fifo_gpio_callback;

static uint8_t fifo_tx[IIM42352_FIFO_MAX_BYTES + 1u];
static uint8_t fifo_rx[IIM42352_FIFO_MAX_BYTES + 1u];
static int16_t parsed_samples[IIM42352_FIFO_MAX_SAMPLES];
static int16_t pending_samples[IIM42352_FIFO_MAX_SAMPLES];
static size_t pending_count;

static atomic_t irq_count;
static atomic_t poll_fallbacks;
static atomic_t fifo_reads;
static atomic_t fifo_packets;
static atomic_t samples_captured;
static atomic_t malformed_packets;
static atomic_t spi_errors;
static atomic_t fifo_flushes;
static atomic_t fifo_overflows;
static atomic_t last_error;

static int note_error(int error)
{
	atomic_set(&last_error, error);
	if (error != 0) {
		atomic_inc(&spi_errors);
	}

	return error;
}

static int iim42352_read_reg(uint8_t reg, uint8_t *value)
{
	uint8_t tx_data[2] = {reg | IIM42352_SPI_READ, 0u};
	uint8_t rx_data[2] = {0u, 0u};
	const struct spi_buf tx_buf = {.buf = tx_data, .len = sizeof(tx_data)};
	const struct spi_buf rx_buf = {.buf = rx_data, .len = sizeof(rx_data)};
	const struct spi_buf_set tx_set = {.buffers = &tx_buf, .count = 1u};
	const struct spi_buf_set rx_set = {.buffers = &rx_buf, .count = 1u};
	int ret;

	if (value == NULL) {
		return -EINVAL;
	}

	ret = spi_transceive_dt(&mems_spi, &tx_set, &rx_set);
	if (ret == 0) {
		*value = rx_data[1];
	}

	return ret;
}

static int iim42352_write_reg(uint8_t reg, uint8_t value)
{
	uint8_t tx_data[2] = {reg & ~IIM42352_SPI_READ, value};
	const struct spi_buf tx_buf = {.buf = tx_data, .len = sizeof(tx_data)};
	const struct spi_buf_set tx_set = {.buffers = &tx_buf, .count = 1u};

	return spi_write_dt(&mems_spi, &tx_set);
}

static int iim42352_update_reg(uint8_t reg, uint8_t mask, uint8_t value)
{
	uint8_t current;
	int ret;

	ret = iim42352_read_reg(reg, &current);
	if (ret != 0) {
		return ret;
	}

	current = (current & ~mask) | (value & mask);
	return iim42352_write_reg(reg, current);
}

static int iim42352_fifo_count_get(uint16_t *count)
{
	uint8_t low;
	uint8_t high;
	int ret;

	if (count == NULL) {
		return -EINVAL;
	}

	/* FIFO_COUNTL must be read first; that access latches both bytes. */
	ret = iim42352_read_reg(IIM42352_REG_FIFO_COUNTL, &low);
	if (ret != 0) {
		return ret;
	}

	ret = iim42352_read_reg(IIM42352_REG_FIFO_COUNTH, &high);
	if (ret != 0) {
		return ret;
	}

	*count = ((uint16_t)high << 8) | low;
	return 0;
}

static int iim42352_fifo_flush(void)
{
	int ret = iim42352_write_reg(IIM42352_REG_SIGNAL_PATH_RESET, BIT(1));

	if (ret == 0) {
		atomic_inc(&fifo_flushes);
		k_sleep(K_MSEC(1));
	}

	return ret;
}

static int iim42352_fifo_burst_read(uint16_t length)
{
	const struct spi_buf tx_buf = {
		.buf = fifo_tx,
		.len = (size_t)length + 1u,
	};
	const struct spi_buf rx_buf = {
		.buf = fifo_rx,
		.len = (size_t)length + 1u,
	};
	const struct spi_buf_set tx_set = {.buffers = &tx_buf, .count = 1u};
	const struct spi_buf_set rx_set = {.buffers = &rx_buf, .count = 1u};

	if (length == 0u || length > IIM42352_FIFO_MAX_BYTES) {
		return -EINVAL;
	}

	fifo_tx[0] = IIM42352_REG_FIFO_DATA | IIM42352_SPI_READ;
	memset(&fifo_tx[1], 0, length);
	return spi_transceive_dt(&mems_spi, &tx_set, &rx_set);
}

static size_t parse_fifo_packets(uint16_t length)
{
	size_t parsed_count = 0u;
	uint16_t packet_count = length / IIM42352_FIFO_PACKET_SIZE;

	for (uint16_t index = 0u; index < packet_count; index++) {
		const uint8_t *packet =
			&fifo_rx[1u + (index * IIM42352_FIFO_PACKET_SIZE)];

		/* Packet 1 has header 0100_00xx; the low two bits are message bits. */
		if ((packet[0] & 0xfcu) != 0x40u) {
			atomic_inc(&malformed_packets);
			continue;
		}

		parsed_samples[parsed_count++] =
			(int16_t)(((uint16_t)packet[1] << 8) | packet[2]);
		parsed_samples[parsed_count++] =
			(int16_t)(((uint16_t)packet[3] << 8) | packet[4]);
		parsed_samples[parsed_count++] =
			(int16_t)(((uint16_t)packet[5] << 8) | packet[6]);
		atomic_inc(&fifo_packets);
	}

	return parsed_count;
}

static void fifo_interrupt_callback(const struct device *port,
				    struct gpio_callback *callback,
				    uint32_t pins)
{
	ARG_UNUSED(port);
	ARG_UNUSED(callback);
	ARG_UNUSED(pins);

	atomic_inc(&irq_count);
	k_sem_give(&fifo_irq_sem);
}

int adc_sampler_init(void)
{
	uint8_t who_am_i;
	uint8_t status;
	int ret;

	pending_count = 0u;
	atomic_clear(&irq_count);
	atomic_clear(&poll_fallbacks);
	atomic_clear(&fifo_reads);
	atomic_clear(&fifo_packets);
	atomic_clear(&samples_captured);
	atomic_clear(&malformed_packets);
	atomic_clear(&spi_errors);
	atomic_clear(&fifo_flushes);
	atomic_clear(&fifo_overflows);
	atomic_clear(&last_error);
	k_sem_reset(&fifo_irq_sem);

	if (!spi_is_ready_dt(&mems_spi) || !gpio_is_ready_dt(&mems_int1)) {
		return note_error(-ENODEV);
	}

	ret = iim42352_read_reg(IIM42352_REG_WHO_AM_I, &who_am_i);
	if (ret != 0) {
		return note_error(ret);
	}
	if (who_am_i != IIM42352_WHO_AM_I_VALUE) {
		return note_error(-ENODEV);
	}

	/* Push-pull, active-low, pulsed INT1. */
	ret = iim42352_write_reg(IIM42352_REG_INT_CONFIG, BIT(1));
	if (ret != 0) {
		return note_error(ret);
	}

	ret = iim42352_fifo_flush();
	if (ret != 0) {
		return note_error(ret);
	}

	/* FIFO count and watermark are expressed in bytes. */
	ret = iim42352_update_reg(IIM42352_REG_INTF_CONFIG0, BIT(6), 0u);
	if (ret != 0) {
		return note_error(ret);
	}

	ret = iim42352_write_reg(IIM42352_REG_FIFO_CONFIG, BIT(6));
	if (ret != 0) {
		return note_error(ret);
	}

	/* Packet 1 plus repeated threshold events while FIFO remains above WM. */
	ret = iim42352_write_reg(IIM42352_REG_FIFO_CONFIG1, BIT(5) | 0x01u);
	if (ret != 0) {
		return note_error(ret);
	}

	ret = iim42352_write_reg(IIM42352_REG_FIFO_CONFIG2,
				 (uint8_t)(IIM42352_FIFO_WATERMARK_BYTES & 0xffu));
	if (ret != 0) {
		return note_error(ret);
	}
	ret = iim42352_write_reg(IIM42352_REG_FIFO_CONFIG3,
				 (uint8_t)(IIM42352_FIFO_WATERMARK_BYTES >> 8));
	if (ret != 0) {
		return note_error(ret);
	}

	/* +/-16 g full scale and 4 kHz ODR in low-noise mode. */
	ret = iim42352_write_reg(IIM42352_REG_ACCEL_CONFIG0, 0x04u);
	if (ret != 0) {
		return note_error(ret);
	}
	ret = iim42352_write_reg(IIM42352_REG_PWR_MGMT0, 0x03u);
	if (ret != 0) {
		return note_error(ret);
	}
	k_sleep(K_MSEC(30));

	/* At ODR >= 4 kHz the datasheet requires the 8 us interrupt pulse. */
	ret = iim42352_update_reg(IIM42352_REG_INT_CONFIG1, BIT(6), BIT(6));
	if (ret != 0) {
		return note_error(ret);
	}

	ret = gpio_pin_configure_dt(&mems_int1, GPIO_INPUT);
	if (ret != 0) {
		return note_error(ret);
	}
	gpio_init_callback(&fifo_gpio_callback, fifo_interrupt_callback,
			   BIT(mems_int1.pin));
	ret = gpio_add_callback(mems_int1.port, &fifo_gpio_callback);
	if (ret != 0) {
		return note_error(ret);
	}
	ret = gpio_pin_interrupt_configure_dt(&mems_int1, GPIO_INT_EDGE_TO_ACTIVE);
	if (ret != 0) {
		return note_error(ret);
	}

	/* Clear stale status before routing new FIFO threshold events to INT1. */
	ret = iim42352_read_reg(IIM42352_REG_INT_STATUS, &status);
	if (ret != 0) {
		return note_error(ret);
	}
	ret = iim42352_write_reg(IIM42352_REG_INT_SOURCE0, BIT(2));
	if (ret != 0) {
		return note_error(ret);
	}

	return 0;
}

int adc_sampler_read_samples(int16_t *samples, size_t sample_count)
{
	size_t produced = 0u;

	if (samples == NULL || sample_count == 0u) {
		return -EINVAL;
	}

	if (pending_count > 0u) {
		size_t copied = MIN(sample_count, pending_count);

		memcpy(samples, pending_samples, copied * sizeof(samples[0]));
		produced = copied;
		atomic_add(&samples_captured, (atomic_val_t)copied);
		pending_count -= copied;
		if (pending_count > 0u) {
			memmove(pending_samples, &pending_samples[copied],
				pending_count * sizeof(pending_samples[0]));
		}
	}

	while (produced < sample_count) {
		uint16_t fifo_bytes;
		uint8_t int_status;
		size_t parsed_count;
		size_t copied;
		int ret;

		ret = k_sem_take(&fifo_irq_sem, K_MSEC(IIM42352_IRQ_WAIT_MS));
		if (ret != 0) {
			/* Polling also recovers if a short 8 us GPIO edge was missed. */
			atomic_inc(&poll_fallbacks);
		}

		ret = iim42352_read_reg(IIM42352_REG_INT_STATUS, &int_status);
		if (ret != 0) {
			return note_error(ret);
		}
		if ((int_status & BIT(1)) != 0u) {
			atomic_inc(&fifo_overflows);
		}

		ret = iim42352_fifo_count_get(&fifo_bytes);
		if (ret != 0) {
			return note_error(ret);
		}
		if (fifo_bytes == 0u) {
			continue;
		}
		if (fifo_bytes > IIM42352_FIFO_MAX_BYTES ||
		    (fifo_bytes % IIM42352_FIFO_PACKET_SIZE) != 0u) {
			atomic_inc(&malformed_packets);
			ret = iim42352_fifo_flush();
			if (ret != 0) {
				return note_error(ret);
			}
			continue;
		}

		ret = iim42352_fifo_burst_read(fifo_bytes);
		if (ret != 0) {
			return note_error(ret);
		}
		atomic_inc(&fifo_reads);

		parsed_count = parse_fifo_packets(fifo_bytes);
		if (parsed_count == 0u) {
			continue;
		}

		copied = MIN(sample_count - produced, parsed_count);
		memcpy(&samples[produced], parsed_samples,
		       copied * sizeof(samples[0]));
		produced += copied;
		atomic_add(&samples_captured, (atomic_val_t)copied);

		pending_count = parsed_count - copied;
		if (pending_count > 0u) {
			memcpy(pending_samples, &parsed_samples[copied],
			       pending_count * sizeof(pending_samples[0]));
		}
	}

	return 0;
}

void adc_sampler_stats_get(struct adc_sampler_stats *stats)
{
	if (stats == NULL) {
		return;
	}

	stats->irq_count = (uint32_t)atomic_get(&irq_count);
	stats->poll_fallbacks = (uint32_t)atomic_get(&poll_fallbacks);
	stats->fifo_reads = (uint32_t)atomic_get(&fifo_reads);
	stats->fifo_packets = (uint32_t)atomic_get(&fifo_packets);
	stats->samples_captured = (uint32_t)atomic_get(&samples_captured);
	stats->malformed_packets = (uint32_t)atomic_get(&malformed_packets);
	stats->spi_errors = (uint32_t)atomic_get(&spi_errors);
	stats->fifo_flushes = (uint32_t)atomic_get(&fifo_flushes);
	stats->fifo_overflows = (uint32_t)atomic_get(&fifo_overflows);
	stats->last_error = (int32_t)atomic_get(&last_error);
}
