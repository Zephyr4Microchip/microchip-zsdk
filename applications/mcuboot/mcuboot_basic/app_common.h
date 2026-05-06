/*
 * Copyright (c) 2026 Microchip Technology Inc.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Common definitions and functions for MCUboot demo applications.
 * This header provides a shared interface for firmware update triggers
 * across all application versions.
 */

#ifndef APP_COMMON_H
#define APP_COMMON_H

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/sys/reboot.h>

/* Application States */
typedef enum {
	APP_STATE_INIT,              /* Initialize peripherals and display version */
	APP_STATE_SERVICE_TASKS,     /* Monitor inputs and blink LED */
	APP_STATE_TRIGGER_UPDATE,    /* Request upgrade and reboot */
} app_state_t;

/* Application Data Structure */
typedef struct {
	app_state_t state;
	bool update_requested;
	uint8_t current_version;
	uint8_t target_version;
	uint32_t blink_interval_ms;
} app_data_t;

/**
 * @brief Trigger firmware update to next version
 *
 * This function requests MCUboot to perform an image swap on next reboot.
 * It provides visual and serial feedback, then initiates a system reset.
 *
 * @param led LED GPIO spec for visual feedback
 * @param target_version Version number to upgrade to
 *
 * @note If slot1 is empty or contains an invalid image, MCUboot will skip
 *       the swap and boot slot0 again. No harm done - the system remains stable.
 */
static inline void app_trigger_update(const struct gpio_dt_spec *led, uint8_t target_version)
{
	printk("\nUpgrade to v%d.0.0 requested\n", target_version);

	/* Request MCUboot to test the update (with rollback capability) */
	boot_request_upgrade(BOOT_UPGRADE_TEST);

	/* Visual confirmation: 6 rapid blinks */
	for (int i = 0; i < 6; i++) {
		gpio_pin_set_dt(led, i % 2);
		k_msleep(100);
	}

	/* Countdown */
	printk("Rebooting in 3...\n");
	k_msleep(1000);
	printk("2...\n");
	k_msleep(1000);
	printk("1...\n");
	k_msleep(1000);

	/* Reboot to bootloader */
	sys_reboot(SYS_REBOOT_COLD);
}

/**
 * @brief Perform startup LED sequence
 *
 * @param led LED GPIO spec
 */
static inline void app_startup_blink(const struct gpio_dt_spec *led)
{
	/* 3 quick blinks to indicate startup */
	for (int i = 0; i < 3; i++) {
		gpio_pin_set_dt(led, 1);
		k_msleep(100);
		gpio_pin_set_dt(led, 0);
		k_msleep(100);
	}
}

/**
 * @brief Confirm current firmware image
 *
 * Marks the current image as good, preventing rollback.
 *
 * @param version Current version number
 */
static inline void app_confirm_image(uint8_t version)
{
	boot_write_img_confirmed();
	printk("Image v%d.0.0 confirmed\n\n", version);
}

#endif /* APP_COMMON_H */
