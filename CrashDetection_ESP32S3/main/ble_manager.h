#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the BLE manager, starting the NimBLE stack and GATT server.
 */
void ble_manager_init(void);

#ifdef __cplusplus
}
#endif
