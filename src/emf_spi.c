#include "emf_spi.h"

#include <errno.h>
#include <string.h>

#include <hal/nrf_gpio.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/toolchain.h>

#include "emf_data.h"

#define EMF_SPI_NODE DT_NODELABEL(spi00)
#ifndef EMF_SPIS_SCK_PORT
#define EMF_SPIS_SCK_PORT 2
#endif
#ifndef EMF_SPIS_SCK_PIN_NUM
#define EMF_SPIS_SCK_PIN_NUM 1
#endif
#ifndef EMF_SPIS_MOSI_PORT
#define EMF_SPIS_MOSI_PORT 2
#endif
#ifndef EMF_SPIS_MOSI_PIN_NUM
#define EMF_SPIS_MOSI_PIN_NUM 2
#endif
#ifndef EMF_SPIS_CSN_PORT
#define EMF_SPIS_CSN_PORT 2
#endif
#ifndef EMF_SPIS_CSN_PIN_NUM
#define EMF_SPIS_CSN_PIN_NUM 4
#endif
#ifndef EMF_SPIS_BOARD_NAME
#define EMF_SPIS_BOARD_NAME "board"
#endif

#define EMF_SPIS_SCK_PIN NRF_GPIO_PIN_MAP(EMF_SPIS_SCK_PORT, EMF_SPIS_SCK_PIN_NUM)
#define EMF_SPIS_MOSI_PIN NRF_GPIO_PIN_MAP(EMF_SPIS_MOSI_PORT, EMF_SPIS_MOSI_PIN_NUM)
#define EMF_SPIS_CSN_PIN NRF_GPIO_PIN_MAP(EMF_SPIS_CSN_PORT, EMF_SPIS_CSN_PIN_NUM)
#define EMF_SPI_RX_THREAD_STACK_SIZE 4096
#define EMF_SPI_RX_THREAD_PRIORITY 5

static const struct device *const emf_spi = DEVICE_DT_GET(EMF_SPI_NODE);

static const struct spi_config emf_spi_config = {
	.frequency = 500000,
	.operation = SPI_OP_MODE_SLAVE | SPI_WORD_SET(8) |
		     SPI_TRANSFER_MSB | SPI_MODE_CPHA,
};

static uint8_t payload[EMF_PAYLOAD_SIZE] __aligned(4);
static volatile bool frame_ready;
static volatile bool frame_received_event;
static volatile bool rx_armed;
static volatile int last_rx_error;
static volatile uint32_t xfer_done_count;
static volatile uint32_t rx_error_count;
static bool spi_initialized;
static uint32_t frame_count;
static uint8_t last_logged_csn = 0xff;
static uint32_t last_logged_xfer_done_count;
static uint32_t last_logged_rx_error_count;

static K_THREAD_STACK_DEFINE(emf_spi_rx_stack, EMF_SPI_RX_THREAD_STACK_SIZE);
static struct k_thread emf_spi_rx_thread;

static uint32_t payload_checksum(void)
{
	uint32_t checksum = 0;

	for (size_t i = 0; i < EMF_PAYLOAD_SIZE; i++) {
		checksum += payload[i];
	}

	return checksum;
}

static void emf_spi_rx_thread_fn(void *p1, void *p2, void *p3)
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

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	while (1) {
		if (frame_ready) {
			k_sleep(K_MSEC(1));
			continue;
		}

		memset(payload, 0, sizeof(payload));
		rx_armed = true;

		err = spi_read(emf_spi, &emf_spi_config, &rx_set);

		rx_armed = false;
		last_rx_error = err;

		if (err) {
			rx_error_count++;
			k_sleep(K_MSEC(10));
			continue;
		}

		xfer_done_count++;
		frame_ready = true;
		frame_received_event = true;
	}
}

int emf_spi_init(void)
{
	if (spi_initialized) {
		return 0;
	}

	if (!device_is_ready(emf_spi)) {
		printk("EMF SPIS device is not ready\n");
		return -ENODEV;
	}

	spi_initialized = true;

	k_thread_create(&emf_spi_rx_thread,
			emf_spi_rx_stack,
			K_THREAD_STACK_SIZEOF(emf_spi_rx_stack),
			emf_spi_rx_thread_fn,
			NULL,
			NULL,
			NULL,
			K_PRIO_PREEMPT(EMF_SPI_RX_THREAD_PRIORITY),
			0,
			K_NO_WAIT);

	printk("EMF SPIS ready: %s spi00 slave, CSN=P%u.%u, SCK=P%u.%u, MOSI=P%u.%u, mode=1, frame=%u bytes\n",
	       EMF_SPIS_BOARD_NAME,
	       EMF_SPIS_CSN_PORT,
	       EMF_SPIS_CSN_PIN_NUM,
	       EMF_SPIS_SCK_PORT,
	       EMF_SPIS_SCK_PIN_NUM,
	       EMF_SPIS_MOSI_PORT,
	       EMF_SPIS_MOSI_PIN_NUM,
	       (unsigned int)EMF_PAYLOAD_SIZE);

	return 0;
}

void emf_spi_poll_debug(void)
{
	uint8_t csn = nrf_gpio_pin_read(EMF_SPIS_CSN_PIN);
	uint8_t sck = nrf_gpio_pin_read(EMF_SPIS_SCK_PIN);
	uint8_t mosi = nrf_gpio_pin_read(EMF_SPIS_MOSI_PIN);

	if (csn != last_logged_csn ||
	    xfer_done_count != last_logged_xfer_done_count ||
	    rx_error_count != last_logged_rx_error_count) {
		printk("EMF SPIS status: armed=%u ready=%u csn=%u sck=%u mosi=%u done=%u errors=%u err=%d first=%02x %02x %02x %02x\n",
		       rx_armed ? 1 : 0,
		       frame_ready ? 1 : 0,
		       csn,
		       sck,
		       mosi,
		       xfer_done_count,
		       rx_error_count,
		       last_rx_error,
		       payload[0],
		       payload[1],
		       payload[2],
		       payload[3]);

		last_logged_csn = csn;
		last_logged_xfer_done_count = xfer_done_count;
		last_logged_rx_error_count = rx_error_count;
	}
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

	frame_count++;
	printk("EMF SPIS frame #%u rx: first=%02x %02x %02x %02x %02x %02x %02x %02x checksum=%u\n",
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
}
