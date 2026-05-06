/*
 * Copyright (c) 2026 Microchip Technology Inc.
 * SPDX-License-Identifier: Apache-2.0
 *
 * MCUboot Demo Application v2.0.0 - FAST BLINK
 *
 * This is the upgraded firmware - LED blinks FAST (250ms)
 * Downgrade prevention is enabled - cannot rollback to v1.0.0
 *
 * Firmware Upgrade Demo:
 *   - Press SW0 button OR enter 'y' via serial to upgrade to v3.0.0
 *   - You can flash app_v3 to slot1 and trigger update from here
 *
 * State Machine Architecture:
 *   - APP_STATE_INIT: Initialize peripherals and confirm image
 *   - APP_STATE_SERVICE_TASKS: Monitor inputs and blink LED
 *   - APP_STATE_TRIGGER_UPDATE: Request upgrade and reboot
 */

#include "app_common.h"

#define APP_VERSION        2
#define TARGET_VERSION     3
#define BLINK_INTERVAL_MS  250   /* v2: FAST blink */

/* LED0 (GREEN) */
#define LED0_NODE DT_ALIAS(led0)
static const struct gpio_dt_spec led0 = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

/* SW0 button */
#define SW0_NODE DT_ALIAS(sw0)
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(SW0_NODE, gpios);

/* UART for serial input */
static const struct device *uart_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

/* Application data */
static app_data_t app_data;

/* Application state machine */


static void app_state_init(void)
{
	/* Startup LED sequence */
	app_startup_blink(&led0);

	/* Allow GPIO to stabilize */
	k_msleep(100);

	/* Display banner */
	printk("\n");
	printk("MCUboot Demo v%d.0.0 - Fast Blink (250ms)\n", app_data.current_version);
	printk("Upgrade successful!\n");
	printk("Press SW0 or 'y' to upgrade to v%d.0.0\n", app_data.target_version);
	printk("\n");

	/* Confirm current image to prevent rollback */
	app_confirm_image(app_data.current_version);
}

static bool app_state_service_tasks(void)
{
	static int led_state = 0;
	uint8_t uart_char;

	/* Toggle LED */
	led_state = !led_state;
	gpio_pin_set_dt(&led0, led_state);

	/* Check button press (gpio_pin_get_dt() returns 1 when pressed) */
	if (!app_data.update_requested && gpio_pin_get_dt(&button) == 1) {
		/* Debounce */
		k_msleep(50);
		/* Confirm press */
		if (gpio_pin_get_dt(&button) == 1) {
			return true;  /* Skip sleep to immediately trigger update */
		}
	}

	/* Check serial input */
	if (!app_data.update_requested && uart_poll_in(uart_dev, &uart_char) == 0) {
		if (uart_char == 'y' || uart_char == 'Y') {
			return true;  /* Skip sleep to immediately trigger update */
		}
	}

	k_msleep(app_data.blink_interval_ms);
	return false;
}

/* Main application function */
int main(void)
{
	/* Initialize application data */
	app_data.state = APP_STATE_INIT;
	app_data.update_requested = false;
	app_data.current_version = APP_VERSION;
	app_data.target_version = TARGET_VERSION;
	app_data.blink_interval_ms = BLINK_INTERVAL_MS;

	/* Configure peripherals */
	gpio_pin_configure_dt(&led0, GPIO_OUTPUT_ACTIVE);
	gpio_pin_configure_dt(&button, GPIO_INPUT);

	/* Main application loop */
	while (1) {
		switch (app_data.state) {
		case APP_STATE_INIT:
			app_state_init();
			/* Transition to service tasks */
			app_data.state = APP_STATE_SERVICE_TASKS;
			break;

		case APP_STATE_SERVICE_TASKS:
			if (app_state_service_tasks() == true) {
				app_data.update_requested = true;
				/* Transition to trigger update if requested */
				app_data.state = APP_STATE_TRIGGER_UPDATE;
			}
			break;

		case APP_STATE_TRIGGER_UPDATE:
			app_trigger_update(&led0, app_data.target_version);
			/* app_trigger_update() never returns (system resets) */
			break;

		default:
			/* Should never reach here */
			printk("ERROR: Invalid state %d\n", app_data.state);
			app_data.state = APP_STATE_INIT;
			break;
		}
	}

	return 0;
}