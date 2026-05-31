#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bmp581.h"
#include "i2c_bus.h"
#include "lsm6dso.h"
#include "max30102.h"
#include "mmc56x3.h"
#include "tmp117.h"
#include "uart_comm.h"
#include "battery_monitor.h"
#include "display_oled.h"
#include "network_sos.h"

#include "driver/gpio.h"

#define GPIO_OUT_CRASH_ALERT 2
#define GPIO_IN_CRASH_CANCEL 3

////////////////////////////////////////////////////////////
// CRASH-DETECTION FLAGS
////////////////////////////////////////////////////////////

volatile int shock_flag = 0;
volatile int warning_flag = 0;
volatile int altitude_flag = 0;
volatile int mag_anomaly_flag = 0;
volatile int vital_crash_flag = 0;
volatile int current_bpm = 0;

volatile int fall_confirmed_flag = 0; // Only used for OLED display status

////////////////////////////////////////////////////////////
// ALTITUDE MONITORING CONFIG
////////////////////////////////////////////////////////////

#define ALTITUDE_DROP_THRESHOLD 2.0f // meters

////////////////////////////////////////////////////////////
// IMU TASK - shock/impact detection
////////////////////////////////////////////////////////////

void imu_task(void *arg) {
  lsm6dso_init();

  while (1) {
    float magnitude = lsm6dso_read_accel_magnitude();

    // Lowered threshold to 10.0g to ensure throwing it on the ground always detects
    if (magnitude > 10.0f) {
      shock_flag = 1;
      printf("SEVERE IMPACT (%.2fg)\n", magnitude);
      // Wait a moment so we don't spam the flag
      vTaskDelay(pdMS_TO_TICKS(1000));
    } else if (magnitude > 6.0f) {
      warning_flag = 1;
      // printf("WARNING IMPACT (%.2fg)\n", magnitude);
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

////////////////////////////////////////////////////////////
// BAROMETER TASK - altitude drop detection
////////////////////////////////////////////////////////////

void barometer_task(void *arg) {
  if (!bmp581_init()) {
    printf("[BARO] Init failed - task stopped\n");
    vTaskDelete(NULL);
    return;
  }

  float pressure, temperature;
  float baseline_alt = 0.0f;

  if (bmp581_read(&pressure, &temperature)) {
    baseline_alt = bmp581_pressure_to_altitude(pressure);
    printf("[BARO] Baseline: %.1f m\n", baseline_alt);
  }

  while (1) {
    if (bmp581_read(&pressure, &temperature)) {
      float current_alt = bmp581_pressure_to_altitude(pressure);
      float drop = baseline_alt - current_alt;

      if (drop > ALTITUDE_DROP_THRESHOLD) {
        altitude_flag = 1;
        printf("[BARO] Altitude drop: %.1f m\n", drop);
      }

      baseline_alt = 0.99f * baseline_alt + 0.01f * current_alt;
    }

    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

////////////////////////////////////////////////////////////
// MAGNETOMETER TASK - sensor fusion & anomaly detection
////////////////////////////////////////////////////////////

void magnetometer_task(void *arg) {
  if (!mmc56x3_init()) {
    printf("[MAG] Init failed - task stopped\n");
    vTaskDelete(NULL);
    return;
  }

  while (1) {
    mag_data_t mag = mmc56x3_read();
    
    if (mmc56x3_detect_mag_anomaly(mag)) {
      mag_anomaly_flag = 1;
      // printf("[MAG] Magnetic Anomaly Detected!\n");
    }

    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

////////////////////////////////////////////////////////////
// SUPERVISOR TASK - fall confirmation & logic flow
////////////////////////////////////////////////////////////

void supervisor_task(void *arg) {
  while (1) {
    if (warning_flag) {
      float temp = tmp117_read_temperature();
      printf("[SUPERVISOR] Sending warning alert...\n");
      uart_send_packet(1, current_bpm, temp); // 1 = Warning
      warning_flag = 0;
    }

    if (shock_flag || vital_crash_flag) {
      bool is_crash_confirmed = false;

      if (vital_crash_flag) {
        printf("[SUPERVISOR] Biological crash detected! (No pulse + Temp drop for 30s)\n");
        is_crash_confirmed = true;
      } else {
        printf("[SUPERVISOR] Severe shock detected! Waiting 3s for bouncing/tumbling to stop...\n");
        
        // Wait 3 seconds to let the helmet finish bouncing/rolling on the ground
        vTaskDelay(pdMS_TO_TICKS(3000));
        
        printf("[SUPERVISOR] Checking for stillness...\n");
        
        // Monitor IMU for 2 seconds to verify the rider is unconscious/still
        bool is_still = true;
        for (int i = 0; i < 20; i++) { // 20 * 100ms = 2s
          float mag = lsm6dso_read_accel_magnitude();
          if (mag > 1.5f || mag < 0.5f) {
             is_still = false;
          }
          vTaskDelay(pdMS_TO_TICKS(100));
        }

        if (is_still) {
          printf("[SUPERVISOR] Stillness confirmed.\n");
          is_crash_confirmed = true;
        } else {
          printf("[SUPERVISOR] False alarm (helmet is still moving). Aborting.\n");
        }
      }

      if (is_crash_confirmed) {
        printf("[SUPERVISOR] Triggering crash alert sequence!\n");
        fall_confirmed_flag = 1; // Used by OLED to display "CRASH: YES"
        
        // Output HIGH to Secondary ESP32 to ring buzzer/LED
        printf("[SUPERVISOR] Alerting Secondary ESP32 on GPIO %d\n", GPIO_OUT_CRASH_ALERT);
        gpio_set_level(GPIO_OUT_CRASH_ALERT, 1);
        
        // Wait 10 seconds for user cancellation
        bool cancelled = false;
        printf("[SUPERVISOR] Waiting 10s for cancellation on GPIO %d...\n", GPIO_IN_CRASH_CANCEL);
        
        for (int i = 0; i < 100; i++) { // 100 * 100ms = 10s
          if (gpio_get_level(GPIO_IN_CRASH_CANCEL) == 1) {
            cancelled = true;
            break;
          }
          vTaskDelay(pdMS_TO_TICKS(100));
        }

        if (cancelled) {
          printf("[SUPERVISOR] CRASH CANCELLED by Secondary ESP32! Aborting SOS.\n");
        } else {
          float temp = tmp117_read_temperature();
          printf("[SUPERVISOR] SOS NOT CANCELLED. Triggering Network Alert!\n");
          
          // Send SOS to App (WiFi/HTTP)
          network_send_sos_alert(true, battery_get_percentage(), temp);
          
          // Send SOS via UART
          uart_send_packet(2, current_bpm, temp); // 2 = Severe Crash
        }

        // Cleanup after event
        gpio_set_level(GPIO_OUT_CRASH_ALERT, 0);
        fall_confirmed_flag = 0;
      }

      // Reset flags so we don't trigger repeatedly
      shock_flag = 0;
      altitude_flag = 0;
      mag_anomaly_flag = 0;
      vital_crash_flag = 0;
    }

    vTaskDelay(pdMS_TO_TICKS(200));
  }
}

// END OF SUPERVISOR TASK

////////////////////////////////////////////////////////////
// PULSE TASK - heart rate + temperature monitoring
////////////////////////////////////////////////////////////

void pulse_task(void *arg) {
  max30102_init();
  tmp117_init();

  uint32_t samples[200];
  for(int i = 0; i < 200; i++) {
      samples[i] = 0;
  }
  
  int sample_idx = 0;
  int zero_bpm_counter = 0;
  float initial_temp = 0.0f;

  // Keep it awake continuously for testing
  max30102_start();

  while (1) {
    // Read 1 sample at 100Hz
    samples[sample_idx] = max30102_read_ir();
    sample_idx = (sample_idx + 1) % 200;

    // Every 50 samples (0.5 seconds), run the highly-accurate BPM algorithm
    if (sample_idx % 50 == 0) {
        // Flatten the circular buffer into a linear array for the algorithm
        uint32_t linear_window[200];
        for(int i = 0; i < 200; i++) {
            linear_window[i] = samples[(sample_idx + i) % 200];
        }

        int bpm = max30102_compute_bpm(linear_window, 200);
        float temp = tmp117_read_temperature();
        
        // Update global BPM and check vital signs
        if (bpm > 0) {
          current_bpm = bpm;
          zero_bpm_counter = 0; // Rider is alive, reset crash counter
        } else {
          // Give a small grace period before snapping to 0 BPM to prevent flickering
          zero_bpm_counter++;
          if (zero_bpm_counter > 3) {
              current_bpm = 0;
          }
          
          // If this is the first time pulse dropped, snapshot the temperature
          if (zero_bpm_counter == 1) {
             initial_temp = temp;
          }
          
          // 60 cycles of 0.5 seconds = 30 consecutive seconds of no pulse
          if (zero_bpm_counter >= 60) {
              // ONLY trigger if the initial temperature was human-like (>30C).
              // If the helmet is just sitting on a desk (e.g. 25C), ignore it so it doesn't automatically SOS!
              if (initial_temp > 30.0f && temp < initial_temp) {
                 printf("[PULSE] VITAL CRASH CONDITION MET! Pulse 0 for 30s + Temp dropped (%.1fC -> %.1fC)\n", initial_temp, temp);
                 vital_crash_flag = 1;
              }
              zero_bpm_counter = 0; 
          }
        }
        
        // Send routine update to secondary ESP32
        uart_send_packet(0, current_bpm, temp); 
    }

    // Delay 10ms (100Hz)
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

////////////////////////////////////////////////////////////
// DISPLAY & SYSTEM STATUS TASK
////////////////////////////////////////////////////////////

// DISPLAY TASK STARTS HERE

void display_task(void *arg) {
  while (1) {
    int batt = battery_get_percentage();
    float batt_v = battery_get_voltage();
    bool crash_status = (shock_flag || warning_flag || fall_confirmed_flag);
    bool online_status = network_is_connected(); 

    // Update OLED Display
    display_update_status(online_status, crash_status, batt);

    // Read live sensor data for Serial Monitor Dashboard
    float acc_mag = lsm6dso_read_accel_magnitude();
    
    float pressure = 0, baro_temp = 0;
    bmp581_read(&pressure, &baro_temp);
    float alt = bmp581_pressure_to_altitude(pressure);
    
    mag_data_t mag = mmc56x3_read();
    float body_temp = tmp117_read_temperature();

    // Print clear, consolidated dashboard every 1 second
    printf("\n================ SENSOR DASHBOARD ================\n");
    printf("IMU    : Accel Mag = %.2f g\n", acc_mag);
    printf("BARO   : Altitude = %.1f m  |  Pressure = %.1f Pa\n", alt, pressure);
    printf("MAG    : X=%.1f  Y=%.1f  Z=%.1f uT\n", mag.x, mag.y, mag.z);
    printf("PULSE  : %d BPM  |  TEMP: %.1f C\n", current_bpm, body_temp);
    printf("POWER  : %.2f V  (%d%%)\n", batt_v, batt);
    printf("SYSTEM : WiFi=%s  |  CRASH FLAGS: Shock=%d Warn=%d Mag=%d\n", 
           online_status ? "ONLINE" : "OFFLINE", shock_flag, warning_flag, mag_anomaly_flag);
    printf("==================================================\n");

    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

////////////////////////////////////////////////////////////
// MAIN ENTRY POINT
////////////////////////////////////////////////////////////

void app_main(void) {
  printf("HELMET ENGINE STARTED\n");

  // Configure Crash Alert GPIOs
  gpio_config_t io_conf = {0};
  io_conf.intr_type = GPIO_INTR_DISABLE;
  io_conf.mode = GPIO_MODE_OUTPUT;
  io_conf.pin_bit_mask = (1ULL << GPIO_OUT_CRASH_ALERT);
  io_conf.pull_down_en = 0;
  io_conf.pull_up_en = 0;
  gpio_config(&io_conf);
  gpio_set_level(GPIO_OUT_CRASH_ALERT, 0);

  io_conf.mode = GPIO_MODE_INPUT;
  io_conf.pin_bit_mask = (1ULL << GPIO_IN_CRASH_CANCEL);
  io_conf.pull_down_en = 1; // Pulldown prevents floating false-positives
  io_conf.pull_up_en = 0;
  gpio_config(&io_conf);

  // Initialize Wi-Fi in the background
  network_wifi_init();

  i2c_bus_init();
  uart_comm_init();
  battery_monitor_init();
  display_oled_init();

  xTaskCreate(imu_task, "IMU", 4096, NULL, 4, NULL);
  xTaskCreate(barometer_task, "BARO", 4096, NULL, 3, NULL);
  xTaskCreate(magnetometer_task, "MAG", 4096, NULL, 3, NULL);
  xTaskCreate(supervisor_task, "SUPERVISOR", 4096, NULL, 2, NULL);
  xTaskCreate(pulse_task, "PULSE", 8192, NULL, 1, NULL);
  xTaskCreate(display_task, "DISPLAY", 4096, NULL, 1, NULL);
}
