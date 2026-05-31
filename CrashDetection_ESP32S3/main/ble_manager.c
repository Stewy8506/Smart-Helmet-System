#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_nimble_hci.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include "ble_manager.h"
#include "lsm6dso.h"

#define TAG "BLE_MGR"

// Device name
#define DEVICE_NAME "SmartHelmet"

// 19B10000-E8F2-537E-4F6C-D104768A1214
static const ble_uuid128_t gatt_svr_svc_uuid =
    BLE_UUID128_INIT(0x14, 0x12, 0x8a, 0x76, 0x04, 0xd1, 0x6c, 0x4f, 0x7e, 0x53, 0xf2, 0xe8, 0x00, 0x00, 0xb1, 0x19);

static const ble_uuid128_t gatt_svr_chr_imu_uuid =
    BLE_UUID128_INIT(0x14, 0x12, 0x8a, 0x76, 0x04, 0xd1, 0x6c, 0x4f, 0x7e, 0x53, 0xf2, 0xe8, 0x01, 0x00, 0xb1, 0x19);

static const ble_uuid128_t gatt_svr_chr_crash_uuid =
    BLE_UUID128_INIT(0x14, 0x12, 0x8a, 0x76, 0x04, 0xd1, 0x6c, 0x4f, 0x7e, 0x53, 0xf2, 0xe8, 0x02, 0x00, 0xb1, 0x19);

static uint16_t imu_chr_val_handle;
static uint16_t crash_chr_val_handle;

static uint8_t own_addr_type;
static uint16_t current_conn_handle = BLE_HS_CONN_HANDLE_NONE;

// Global flags from main.c
extern volatile int shock_flag;
extern volatile int warning_flag;
extern volatile int fall_confirmed_flag;
extern volatile int vital_crash_flag;

static int gatt_svr_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                               struct ble_gatt_access_ctxt *ctxt, void *arg);

static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &gatt_svr_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &gatt_svr_chr_imu_uuid.u,
                .access_cb = gatt_svr_chr_access,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &imu_chr_val_handle,
            },
            {
                .uuid = &gatt_svr_chr_crash_uuid.u,
                .access_cb = gatt_svr_chr_access,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &crash_chr_val_handle,
            },
            {
                0, /* No more characteristics */
            }
        },
    },
    {
        0, /* No more services */
    },
};

static int get_crash_status(void) {
    if (fall_confirmed_flag) return 4;
    if (vital_crash_flag) return 3;
    if (shock_flag) return 2;
    if (warning_flag) return 1;
    return 0;
}

static int gatt_svr_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                               struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    char buf[64];
    int rc;

    if (attr_handle == imu_chr_val_handle) {
        accel_data_t acc = lsm6dso_read_accel_xyz();
        snprintf(buf, sizeof(buf), "%.2f,%.2f,%.2f", acc.x, acc.y, acc.z);
        rc = os_mbuf_append(ctxt->om, buf, strlen(buf));
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    } else if (attr_handle == crash_chr_val_handle) {
        snprintf(buf, sizeof(buf), "%d", get_crash_status());
        rc = os_mbuf_append(ctxt->om, buf, strlen(buf));
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    return BLE_ATT_ERR_UNLIKELY;
}

int ble_gap_event(struct ble_gap_event *event, void *arg);

static void ble_app_advertise(void)
{
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;
    const char *name;
    int rc;

    memset(&fields, 0, sizeof fields);
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;

    name = ble_svc_gap_device_name();
    fields.name = (uint8_t *)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "error setting advertisement data; rc=%d", rc);
        return;
    }

    memset(&adv_params, 0, sizeof adv_params);
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    
    rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER,
                           &adv_params, ble_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "error enabling advertisement; rc=%d", rc);
        return;
    }
}

int ble_gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI(TAG, "connection %s; status=%d",
                 event->connect.status == 0 ? "established" : "failed",
                 event->connect.status);
        if (event->connect.status == 0) {
            current_conn_handle = event->connect.conn_handle;
        } else {
            current_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            ble_app_advertise();
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "disconnect; reason=%d", event->disconnect.reason);
        current_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        ble_app_advertise();
        return 0;
        
    case BLE_GAP_EVENT_SUBSCRIBE:
        ESP_LOGI(TAG, "subscribe event; conn_handle=%d attr_handle=%d",
                 event->subscribe.conn_handle, event->subscribe.attr_handle);
        return 0;
    }
    return 0;
}

static void ble_app_on_sync(void)
{
    int rc;

    rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "error ensuring address; rc=%d", rc);
    }

    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "error determining address type; rc=%d", rc);
        return;
    }

    ble_app_advertise();
}

static void ble_data_task(void *arg)
{
    char buf[64];
    int last_crash_status = -1;
    
    while (1) {
        if (current_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
            // 1. Send IMU Data (10Hz)
            accel_data_t acc = lsm6dso_read_accel_xyz();
            snprintf(buf, sizeof(buf), "%.2f,%.2f,%.2f", acc.x, acc.y, acc.z);
            
            struct os_mbuf *om = ble_hs_mbuf_from_flat(buf, strlen(buf));
            if (om) {
                ble_gatts_notify_custom(current_conn_handle, imu_chr_val_handle, om);
            }
            
            // 2. Send Crash Alert if changed
            int current_crash_status = get_crash_status();
            if (current_crash_status != last_crash_status) {
                last_crash_status = current_crash_status;
                snprintf(buf, sizeof(buf), "%d", current_crash_status);
                om = ble_hs_mbuf_from_flat(buf, strlen(buf));
                if (om) {
                    ble_gatts_notify_custom(current_conn_handle, crash_chr_val_handle, om);
                }
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(100)); // 10Hz
    }
}

static void ble_host_task(void *param)
{
    ESP_LOGI(TAG, "BLE Host Task Started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void ble_manager_init(void)
{
    int rc;

    ESP_LOGI(TAG, "Initializing NimBLE");

    ESP_ERROR_CHECK(esp_nimble_hci_and_controller_init());
    nimble_port_init();

    ble_hs_cfg.sync_cb = ble_app_on_sync;

    ble_svc_gap_init();
    ble_svc_gatt_init();

    rc = ble_gatts_count_cfg(gatt_svr_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "error counting GATT services; rc=%d", rc);
    }

    rc = ble_gatts_add_svcs(gatt_svr_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "error adding GATT services; rc=%d", rc);
    }

    rc = ble_svc_gap_device_name_set(DEVICE_NAME);
    if (rc != 0) {
        ESP_LOGE(TAG, "error setting device name; rc=%d", rc);
    }

    nimble_port_freertos_init(ble_host_task);
    
    // Start data polling task
    xTaskCreate(ble_data_task, "ble_data_task", 4096, NULL, 5, NULL);
}
