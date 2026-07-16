#include "emf_spi.h"

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/toolchain.h>

#include "emf_data.h"

#define EMF_SPI_NODE DT_NODELABEL(spi00)
#define EMF_SPI_TRIGGER_NODE DT_NODELABEL(emf_spi_trigger)
#define EMF_SPI_FREQUENCY_HZ 1200000

static const struct device *const emf_spi = DEVICE_DT_GET(EMF_SPI_NODE);
static const struct gpio_dt_spec emf_trigger =
	GPIO_DT_SPEC_GET(EMF_SPI_TRIGGER_NODE, gpios);

static const struct spi_config emf_spi_config = {
	.frequency = EMF_SPI_FREQUENCY_HZ,
	.operation = SPI_OP_MODE_MASTER | SPI_WORD_SET(8) |
		     SPI_TRANSFER_MSB | SPI_MODE_CPHA,
};

static uint8_t payload[EMF_PAYLOAD_SIZE] __aligned(4);
static volatile bool frame_ready;
static volatile bool frame_received_event;
static volatile bool rx_busy;
static uint32_t frame_count;

static struct gpio_callback emf_trigger_cb;
static struct k_work emf_spi_rx_work;

static uint32_t payload_checksum(void)
{
	uint32_t checksum = 0;

	for (size_t i = 0; i < EMF_PAYLOAD_SIZE; i++) {
		checksum += payload[i];
	}

	return checksum;
}

static void emf_spi_rx_work_handler(struct k_work *work)
{
	struct spi_buf rx_buf = {
		.buf = payload,
		.len = EMF_PAYLOAD_SIZE,
	};
	struct spi_buf_set rx_set = {
		.buffers = &rx_buf,
		.count = 1,
	};
	int err;

	ARG_UNUSED(work);

	if (rx_busy || frame_ready) {
		return;
	}

	rx_busy = true;
	memset(payload, 0, sizeof(payload));

	err = spi_read(emf_spi, &emf_spi_config, &rx_set);
	if (err) {
		printk("EMF SPI master read failed: %d\n", err);
		rx_busy = false;
		return;
	}

	frame_count++;
	printk("EMF SPI master frame #%u rx: first=%02x %02x %02x %02x %02x %02x %02x %02x checksum=%u\n",
	       frame_count,
	       payload[0],
	       payload[1],
	       payload[2],
	       payload[3],
	       payload[4],
	       payload[5],
	       payload[6],
	       payload[7],
	       payload_checksum());

	frame_ready = true;
	frame_received_event = true;
	rx_busy = false;
}

static void emf_trigger_handler(const struct device *dev,
				struct gpio_callback *cb,
				uint32_t pins)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

	k_work_submit(&emf_spi_rx_work);
}

int emf_spi_init(void)
{
	int err;

	if (!device_is_ready(emf_spi)) {
		printk("EMF SPI is not ready\n");
		return -ENODEV;
	}

	if (!gpio_is_ready_dt(&emf_trigger)) {
		printk("EMF SPI trigger GPIO is not ready\n");
		return -ENODEV;
	}

	err = gpio_pin_configure_dt(&emf_trigger, GPIO_INPUT);
	if (err) {
		printk("EMF SPI trigger config failed: %d\n", err);
		return err;
	}

	k_work_init(&emf_spi_rx_work, emf_spi_rx_work_handler);

	gpio_init_callback(&emf_trigger_cb,
			   emf_trigger_handler,
			   BIT(emf_trigger.pin));

	err = gpio_add_callback(emf_trigger.port, &emf_trigger_cb);
	if (err) {
		printk("EMF SPI trigger callback failed: %d\n", err);
		return err;
	}

	err = gpio_pin_interrupt_configure_dt(&emf_trigger,
					      GPIO_INT_EDGE_TO_ACTIVE);
	if (err) {
		printk("EMF SPI trigger interrupt failed: %d\n", err);
		return err;
	}

	printk("EMF SPI master ready: spi00, freq=%u Hz, trigger=P1.6 falling edge, frame=%u bytes\n",
	       EMF_SPI_FREQUENCY_HZ,
	       (unsigned int)EMF_PAYLOAD_SIZE);

	return 0;
}

bool emf_spi_frame_ready(void)
{
	return frame_ready;
}

bool emf_spi_take_frame_received_event(void)
{
	bool event = frame_received_event;

	frame_received_event = false;

	return event;
}

const uint8_t *emf_spi_payload(void)
{
	return payload;
}

void emf_spi_release_frame(void)
{
	frame_ready = false;
}
