#include "emf_uart.h"

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "emf_data.h"

#define EMF_UART_NODE DT_NODELABEL(uart20)

static const struct device *const emf_uart = DEVICE_DT_GET(EMF_UART_NODE);

static uint8_t payload[EMF_PAYLOAD_SIZE];
static volatile size_t rx_len;
static volatile uint32_t error_count;
static volatile int last_error;
static volatile bool frame_ready;
static volatile bool frame_received_event;

static void reset_rx(void)
{
	rx_len = 0;
	uart_irq_rx_enable(emf_uart);
}

static void process_rx_byte(uint8_t byte)
{
	if (rx_len < EMF_PAYLOAD_SIZE) {
		payload[rx_len++] = byte;
	}

	if (rx_len >= EMF_PAYLOAD_SIZE) {
		uart_irq_rx_disable(emf_uart);
		frame_ready = true;
		frame_received_event = true;
	}
}

static void process_rx_data(const uint8_t *data, size_t len)
{
	for (size_t i = 0; i < len; i++) {
		process_rx_byte(data[i]);
		if (frame_ready) {
			break;
		}
	}
}

static void emf_uart_isr(const struct device *dev, void *user_data)
{
	ARG_UNUSED(user_data);
	uint8_t rx_byte;

	if (!uart_irq_update(dev) || !uart_irq_rx_ready(dev)) {
		return;
	}

	while (!frame_ready && uart_fifo_read(dev, &rx_byte, 1) == 1) {
		process_rx_data(&rx_byte, 1);
	}
}

int emf_uart_init(void)
{
	if (!device_is_ready(emf_uart)) {
		printk("EMF UART is not ready\n");
		return -ENODEV;
	}

	uart_irq_callback_user_data_set(emf_uart, emf_uart_isr, NULL);
	reset_rx();

	printk("EMF UART RX ready: uart20, 115200 8N1, frame=%u bytes\n",
	       (unsigned int)EMF_PAYLOAD_SIZE);

	return 0;
}

size_t emf_uart_rx_len(void)
{
	return rx_len;
}

uint32_t emf_uart_error_count(void)
{
	return error_count;
}

int emf_uart_last_error(void)
{
	return last_error;
}

bool emf_uart_frame_ready(void)
{
	return frame_ready;
}

bool emf_uart_take_frame_received_event(void)
{
	bool event = frame_received_event;

	frame_received_event = false;

	return event;
}

const uint8_t *emf_uart_payload(void)
{
	return payload;
}

void emf_uart_release_frame(void)
{
	frame_ready = false;
	reset_rx();
}
