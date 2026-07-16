#include <zephyr/kernel.h>
#include <zephyr/types.h>
#include <zephyr/sys/printk.h>
#include <zephyr/drivers/gpio.h>

#include <string.h>
#include <errno.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>

#include "emf_data.h"
#include "emf_spi.h"
#include "emf_uart.h"

#define EMF_INPUT_USE_SPI 1

/*
 * BLE services:
 *
 * 1) Battery Service
 *    Service UUID:        0x180F
 *    Battery Level UUID:  0x2A19, read + notify
 *    BATS UUID:           0x2BED, read + notify
 *
 * 2) Data Service
 *    Service UUID:        00000000-8e22-4541-9d4c-21edae82ed19
 *    emf_values UUID:     00000001-8e22-4541-9d4c-21edae82ed19
 *    Properties:          indicate
 *    Payload size:        200 bytes
 */

#define EMF_VALUES_SIZE 200

#define DATA_SERVICE_UUID_VAL \
	BT_UUID_128_ENCODE(0x00000000, 0x8e22, 0x4541, 0x9d4c, 0x21edae82ed19)

#define EMF_VALUES_UUID_VAL \
	BT_UUID_128_ENCODE(0x00000001, 0x8e22, 0x4541, 0x9d4c, 0x21edae82ed19)

static struct bt_uuid_128 data_service_uuid =
	BT_UUID_INIT_128(DATA_SERVICE_UUID_VAL);

static struct bt_uuid_128 emf_values_uuid =
	BT_UUID_INIT_128(EMF_VALUES_UUID_VAL);

/* Current BLE connection */
static struct bt_conn *current_conn;

/* Battery values */
static uint8_t battery_level = 95;
static uint8_t battery_status = 0;

/* CCC states */
static bool battery_level_notify_enabled;
static bool battery_status_notify_enabled;
static bool emf_indicate_enabled;

/* EMF data */
static uint8_t emf_values[EMF_VALUES_SIZE];
static size_t emf_payload_offset;
static bool emf_indication_busy;

/* ------------------------------------------------ */
/* EMF input source                                 */
/* ------------------------------------------------ */

#if EMF_INPUT_USE_SPI
#define emf_input_init() emf_spi_init()
#define emf_input_frame_ready() emf_spi_frame_ready()
#define emf_input_take_frame_received_event() emf_spi_take_frame_received_event()
#define emf_input_payload() emf_spi_payload()
#define emf_input_release_frame() emf_spi_release_frame()
#define EMF_INPUT_NAME "SPI"
#else
#define emf_input_init() emf_uart_init()
#define emf_input_frame_ready() emf_uart_frame_ready()
#define emf_input_take_frame_received_event() emf_uart_take_frame_received_event()
#define emf_input_payload() emf_uart_payload()
#define emf_input_release_frame() emf_uart_release_frame()
#define EMF_INPUT_NAME "UART"
#endif

/* ------------------------------------------------ */
/* Status LED                                       */
/* ------------------------------------------------ */

#define STATUS_LED_NODE DT_ALIAS(led0)

#if DT_NODE_HAS_STATUS(STATUS_LED_NODE, okay)
static const struct gpio_dt_spec status_led =
	GPIO_DT_SPEC_GET(STATUS_LED_NODE, gpios);

static struct k_work_delayable status_led_work;
static int status_led_toggles;

static void status_led_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	if (status_led_toggles <= 0) {
		gpio_pin_set_dt(&status_led, 0);
		return;
	}

	gpio_pin_toggle_dt(&status_led);
	status_led_toggles--;
	k_work_schedule(&status_led_work, K_MSEC(50));
}

static int status_led_init(void)
{
	int err;

	if (!gpio_is_ready_dt(&status_led)) {
		printk("Status LED is not ready\n");
		return -ENODEV;
	}

	err = gpio_pin_configure_dt(&status_led, GPIO_OUTPUT_INACTIVE);
	if (err) {
		printk("Status LED config failed: %d\n", err);
		return err;
	}

	k_work_init_delayable(&status_led_work, status_led_work_handler);

	return 0;
}

static void status_led_blink(void)
{
	status_led_toggles = 6;
	gpio_pin_set_dt(&status_led, 0);
	k_work_schedule(&status_led_work, K_NO_WAIT);
}
#else
static int status_led_init(void)
{
	printk("Status LED disabled: led0 alias is not available\n");
	return 0;
}

static void status_led_blink(void)
{
}
#endif

/* ------------------------------------------------ */
/* Read callbacks                                   */
/* ------------------------------------------------ */

static ssize_t read_u8_value(struct bt_conn *conn,
			     const struct bt_gatt_attr *attr,
			     void *buf,
			     uint16_t len,
			     uint16_t offset)
{
	const uint8_t *value = attr->user_data;

	return bt_gatt_attr_read(conn, attr, buf, len, offset,
				 value, sizeof(*value));
}

static ssize_t read_emf_values(struct bt_conn *conn,
			       const struct bt_gatt_attr *attr,
			       void *buf,
			       uint16_t len,
			       uint16_t offset)
{
	const uint8_t *value = attr->user_data;

	return bt_gatt_attr_read(conn, attr, buf, len, offset,
				 value, EMF_VALUES_SIZE);
}

/* ------------------------------------------------ */
/* CCC callbacks                                    */
/* ------------------------------------------------ */

static void battery_level_ccc_changed(const struct bt_gatt_attr *attr,
				      uint16_t value)
{
	battery_level_notify_enabled = (value == BT_GATT_CCC_NOTIFY);

	printk("Battery Level notify: %s\n",
	       battery_level_notify_enabled ? "enabled" : "disabled");
}

static void battery_status_ccc_changed(const struct bt_gatt_attr *attr,
				       uint16_t value)
{
	battery_status_notify_enabled = (value == BT_GATT_CCC_NOTIFY);

	printk("Battery Status notify: %s\n",
	       battery_status_notify_enabled ? "enabled" : "disabled");
}

static void emf_ccc_changed(const struct bt_gatt_attr *attr,
			    uint16_t value)
{
	emf_indicate_enabled = (value == BT_GATT_CCC_INDICATE);

	if (emf_indicate_enabled) {
		emf_payload_offset = 0;
	}

	printk("EMF indication: %s\n",
	       emf_indicate_enabled ? "enabled" : "disabled");
}

/* ------------------------------------------------ */
/* Battery Service                                  */
/* ------------------------------------------------ */

/*
 * battery_svc attrs:
 *
 * [0] Primary Service
 *
 * Battery Level:
 * [1] Characteristic Declaration
 * [2] Characteristic Value
 * [3] Characteristic User Description
 * [4] CCC
 *
 * Battery Status:
 * [5] Characteristic Declaration
 * [6] Characteristic Value
 * [7] Characteristic User Description
 * [8] CCC
 */

BT_GATT_SERVICE_DEFINE(battery_svc,
	BT_GATT_PRIMARY_SERVICE(BT_UUID_DECLARE_16(0x180F)),

	BT_GATT_CHARACTERISTIC(BT_UUID_DECLARE_16(0x2A19),
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ,
			       read_u8_value,
			       NULL,
			       &battery_level),

	BT_GATT_CUD("Battery Level", BT_GATT_PERM_READ),

	BT_GATT_CCC(battery_level_ccc_changed,
		    BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),

	BT_GATT_CHARACTERISTIC(BT_UUID_DECLARE_16(0x2BED),
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ,
			       read_u8_value,
			       NULL,
			       &battery_status),

	BT_GATT_CUD("Battery Status", BT_GATT_PERM_READ),

	BT_GATT_CCC(battery_status_ccc_changed,
		    BT_GATT_PERM_READ | BT_GATT_PERM_WRITE)
);

/* ------------------------------------------------ */
/* Data Service                                     */
/* ------------------------------------------------ */

/*
 * data_svc attrs:
 *
 * [0] Primary Service
 * [1] Characteristic Declaration
 * [2] Characteristic Value
 * [3] Characteristic User Description
 * [4] CCC
 */

BT_GATT_SERVICE_DEFINE(data_svc,
	BT_GATT_PRIMARY_SERVICE(&data_service_uuid.uuid),

	BT_GATT_CHARACTERISTIC(&emf_values_uuid.uuid,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_INDICATE,
			       BT_GATT_PERM_READ,
			       read_emf_values,
			       NULL,
			       emf_values),

	BT_GATT_CUD("EMF Values", BT_GATT_PERM_READ),

	BT_GATT_CCC(emf_ccc_changed,
		    BT_GATT_PERM_READ | BT_GATT_PERM_WRITE)
);

/* ------------------------------------------------ */
/* Battery notify                                   */
/* ------------------------------------------------ */

static void send_battery_level_notify(void)
{
	if (!current_conn || !battery_level_notify_enabled) {
		return;
	}

	int err = bt_gatt_notify(current_conn,
				 &battery_svc.attrs[2],
				 &battery_level,
				 sizeof(battery_level));

	if (err) {
		printk("Battery Level notify failed: %d\n", err);
	}
}

static void send_battery_status_notify(void)
{
	if (!current_conn || !battery_status_notify_enabled) {
		return;
	}

	int err = bt_gatt_notify(current_conn,
				 &battery_svc.attrs[6],
				 &battery_status,
				 sizeof(battery_status));

	if (err) {
		printk("Battery Status notify failed: %d\n", err);
	}
}

/* ------------------------------------------------ */
/* EMF indication                                   */
/* ------------------------------------------------ */

static void emf_indication_complete(struct bt_conn *conn,
				    struct bt_gatt_indicate_params *params,
				    uint8_t err)
{
	emf_indication_busy = false;

	printk("EMF indication complete, err=%u\n", err);
}

static struct bt_gatt_indicate_params emf_ind_params;

static int send_emf_values_indication(const uint8_t *data, uint16_t size)
{
	if (!current_conn) {
		return -ENOTCONN;
	}

	if (!emf_indicate_enabled) {
		return -EACCES;
	}

	if (emf_indication_busy) {
		return -EBUSY;
	}

	if (size > EMF_VALUES_SIZE) {
		return -EINVAL;
	}

	memcpy(emf_values, data, size);

	emf_ind_params.attr = &data_svc.attrs[2];
	emf_ind_params.func = emf_indication_complete;
	emf_ind_params.data = emf_values;
	emf_ind_params.len = size;

	emf_indication_busy = true;

	int err = bt_gatt_indicate(current_conn, &emf_ind_params);

	if (err) {
		emf_indication_busy = false;
		printk("EMF indication failed: %d\n", err);
	}

	return err;
}

static int send_next_emf_chunk(void)
{
	size_t chunk_size;
	int err;

	if (!emf_input_frame_ready()) {
		return -EAGAIN;
	}

	if (emf_payload_offset >= EMF_PAYLOAD_SIZE) {
		emf_input_release_frame();
		return -EAGAIN;
	}

	chunk_size = EMF_PAYLOAD_SIZE - emf_payload_offset;
	if (chunk_size > EMF_VALUES_SIZE) {
		chunk_size = EMF_VALUES_SIZE;
	}

	err = send_emf_values_indication(&emf_input_payload()[emf_payload_offset],
					 chunk_size);
	if (err) {
		return err;
	}

	printk("EMF chunk sent: offset=%u size=%u\n",
	       (unsigned int)emf_payload_offset,
	       (unsigned int)chunk_size);

	emf_payload_offset += chunk_size;

	if (emf_payload_offset >= EMF_PAYLOAD_SIZE) {
		printk("EMF %s frame sent over BLE, size=%u\n",
		       EMF_INPUT_NAME,
		       (unsigned int)EMF_PAYLOAD_SIZE);
		emf_input_release_frame();
	}

	return 0;
}

/* ------------------------------------------------ */
/* Connection callbacks                             */
/* ------------------------------------------------ */

static int start_advertising(void);
static void advertising_work_handler(struct k_work *work);

static K_WORK_DELAYABLE_DEFINE(advertising_work, advertising_work_handler);

static void connected(struct bt_conn *conn, uint8_t err)
{
	if (err) {
		printk("Connection failed, err=%u\n", err);
		return;
	}

	current_conn = bt_conn_ref(conn);

	printk("Connected\n");
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	printk("Disconnected, reason=%u\n", reason);

	if (current_conn) {
		bt_conn_unref(current_conn);
		current_conn = NULL;
	}

	battery_level_notify_enabled = false;
	battery_status_notify_enabled = false;
	emf_indicate_enabled = false;
	emf_indication_busy = false;

	k_work_schedule(&advertising_work, K_MSEC(100));
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
};

/* ------------------------------------------------ */
/* Advertising                                      */
/* ------------------------------------------------ */

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR),

	BT_DATA(BT_DATA_NAME_COMPLETE,
		CONFIG_BT_DEVICE_NAME,
		sizeof(CONFIG_BT_DEVICE_NAME) - 1),

	BT_DATA_BYTES(BT_DATA_UUID16_ALL,
		      BT_UUID_16_ENCODE(0x180F)),
};

static int start_advertising(void)
{
	int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1,
				  ad,
				  ARRAY_SIZE(ad),
				  NULL,
				  0);

	if (err) {
		return err;
	}

	printk("Advertising started as %s\n", CONFIG_BT_DEVICE_NAME);

	return 0;
}

static void advertising_work_handler(struct k_work *work)
{
	int err = start_advertising();

	if (err) {
		printk("Advertising restart failed: %d\n", err);
	}
}

/* ------------------------------------------------ */
/* Main                                             */
/* ------------------------------------------------ */

int main(void)
{
	int err;
	int64_t last_battery_update = -10000;

	printk("Starting BLE GATT server\n");

	err = status_led_init();
	if (err) {
		printk("Status LED init failed: %d\n", err);
	}

	err = bt_enable(NULL);
	if (err) {
		printk("Bluetooth init failed: %d\n", err);
		return 0;
	}

	printk("Bluetooth initialized\n");

	err = start_advertising();
	if (err) {
		printk("Advertising start failed: %d\n", err);
		return 0;
	}

	err = emf_input_init();
	if (err) {
		printk("EMF %s init failed: %d\n", EMF_INPUT_NAME, err);
		return 0;
	}

	while (1) {
		int64_t now = k_uptime_get();

		if (emf_input_take_frame_received_event()) {
			emf_payload_offset = 0;
			status_led_blink();
			printk("EMF %s frame received, size=%u\n",
			       EMF_INPUT_NAME,
			       (unsigned int)EMF_PAYLOAD_SIZE);
		}

		if ((now - last_battery_update) >= 10000) {
			if (battery_level > 0) {
				battery_level--;
			} else {
				battery_level = 100;
			}

			battery_status ^= 0x01;

			send_battery_level_notify();
			send_battery_status_notify();

			last_battery_update = now;
		}

		if (current_conn && emf_indicate_enabled &&
		    !emf_indication_busy && emf_input_frame_ready()) {
			err = send_next_emf_chunk();

			if (err == -EMSGSIZE) {
				printk("EMF chunk too large for current MTU\n");
			} else if (err == -EAGAIN) {
				/* Waiting for the next input frame. */
			} else if (err) {
				printk("EMF chunk error: %d\n", err);
			}
		}

		k_sleep(K_MSEC(10));
	}

	return 0;
}	
