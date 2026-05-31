#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2c_bus.h"
#include "max30102.h"

////////////////////////////////////////////////////////////
// MAX30102 I2C ADDRESS & REGISTERS
////////////////////////////////////////////////////////////

#define MAX30102_ADDR       0x57

#define REG_FIFO_WR_PTR     0x04
#define REG_FIFO_OVR_PTR    0x05
#define REG_FIFO_RD_PTR     0x06
#define REG_FIFO_DATA       0x07
#define REG_FIFO_CONFIG     0x08
#define REG_MODE_CONFIG     0x09
#define REG_SPO2_CONFIG     0x0A
#define REG_LED1_PA         0x0C
#define REG_LED2_PA         0x0D

////////////////////////////////////////////////////////////
// VALIDATION
////////////////////////////////////////////////////////////

#define BPM_MIN_VALID       75
#define BPM_MAX_VALID       130
#define IR_THRESHOLD        70000

////////////////////////////////////////////////////////////
// INIT & POWER MANAGEMENT
////////////////////////////////////////////////////////////

void max30102_init(void)
{
    // Reset (0x40)
    i2c_write_reg(MAX30102_ADDR, REG_MODE_CONFIG, 0x40);
    vTaskDelay(pdMS_TO_TICKS(100));

    // Configure FIFO (Rollover EN = 1)
    i2c_write_reg(MAX30102_ADDR, REG_FIFO_CONFIG, 0x1F);

    // SpO2 config: 100Hz sample rate, 411us pulse width
    i2c_write_reg(MAX30102_ADDR, REG_SPO2_CONFIG, 0x27);

    // INCREASE SENSITIVITY: Boost LED current to 0x7F (~25mA) for better skin penetration
    i2c_write_reg(MAX30102_ADDR, REG_LED1_PA, 0x7F);
    i2c_write_reg(MAX30102_ADDR, REG_LED2_PA, 0x7F);

    // Put into Shutdown mode immediately (0x80)
    i2c_write_reg(MAX30102_ADDR, REG_MODE_CONFIG, 0x80);

    printf("[PULSE] MAX30102 initialized and asleep\n");
}

void max30102_start(void)
{
    // Clear FIFO pointers
    i2c_write_reg(MAX30102_ADDR, REG_FIFO_WR_PTR, 0x00);
    i2c_write_reg(MAX30102_ADDR, REG_FIFO_OVR_PTR, 0x00);
    i2c_write_reg(MAX30102_ADDR, REG_FIFO_RD_PTR, 0x00);

    // Enter HR mode (0x02) - Uses LED1 (Red) only
    i2c_write_reg(MAX30102_ADDR, REG_MODE_CONFIG, 0x02);
}

void max30102_stop(void)
{
    // Put into Shutdown mode (0x80)
    i2c_write_reg(MAX30102_ADDR, REG_MODE_CONFIG, 0x80);
}

////////////////////////////////////////////////////////////
// READ SINGLE SAMPLE
////////////////////////////////////////////////////////////

uint32_t max30102_read_ir(void)
{
    uint8_t data[3];

    // Read 3 bytes from FIFO data register
    i2c_read_bytes(MAX30102_ADDR, REG_FIFO_DATA, data, 3);

    // Construct 24-bit value
    return ((uint32_t)data[0] << 16) |
           ((uint32_t)data[1] << 8)  |
            (uint32_t)data[2];
}

////////////////////////////////////////////////////////////
// COMPUTE BPM FROM SAMPLES (Peak-to-Peak Time Delta)
////////////////////////////////////////////////////////////

int max30102_compute_bpm(uint32_t *samples, int count)
{
    // 1. Dynamic Threshold: Find signal range and average
    uint32_t min_val = 0xFFFFFFFF;
    uint32_t max_val = 0;
    uint64_t avg_val = 0;

    for (int i = 0; i < count; i++) {
        if (samples[i] < min_val) min_val = samples[i];
        if (samples[i] > max_val) max_val = samples[i];
        avg_val += samples[i];
    }
    avg_val /= count;
    
    // If the signal amplitude is too flat, there is no finger present
    if (max_val - min_val < 1000) {
        return -1; 
    }

    // 2. Exact Time-Delta Peak Detection
    int last_peak_idx = -1;
    int bpm_sum = 0;
    int bpm_count = 0;

    for (int i = 2; i < count - 2; i++)
    {
        // Detect a local peak that is ABOVE the running average (filters noise)
        if (samples[i] > samples[i - 1] &&
            samples[i] > samples[i + 1] &&
            samples[i] > samples[i - 2] &&
            samples[i] > samples[i + 2] &&
            samples[i] > avg_val)
        {
            if (last_peak_idx != -1) {
                // Number of 10ms intervals between beats
                int delta = i - last_peak_idx;
                
                // BPM = (60 seconds / (delta * 0.01 seconds)) = 6000 / delta
                int current_bpm = 6000 / delta;
                
                if (current_bpm >= 40 && current_bpm <= 180) {
                    bpm_sum += current_bpm;
                    bpm_count++;
                }
            }
            last_peak_idx = i;
        }
    }

    if (bpm_count > 0) {
        return bpm_sum / bpm_count;
    }

    return -1;
}
