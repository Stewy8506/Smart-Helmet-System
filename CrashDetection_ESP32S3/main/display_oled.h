#pragma once

#include <stdbool.h>

/**
 * Initialize the SSD1306 128x64 OLED Display over I2C.
 * Sends the full initialization sequence and clears the screen.
 * Must be called AFTER i2c_bus_init().
 */
void display_oled_init(void);

/**
 * Update the OLED with the current system status.
 * Redraws the entire screen from an internal framebuffer
 * to prevent artifacts and flickering.
 *
 * @param online     true if Wi-Fi is connected
 * @param crash      true if a crash flag is active
 * @param battery_pct  battery percentage 0–100
 */
void display_update_status(bool online, bool crash, int battery_pct);
