#include "buffer_manager.h"
#include "audio_pipeline.h"

volatile bool is_anc_on = false; // False = Transparency, True = Prototype ANC

// ANC gain and tuning
#define ANC_GAIN 0.8f
#define TRANSPARENCY_GAIN 1.0f

static int16_t delay_buffer[64];
static int delay_index = 0;

void BufferManager_Process(void)
{
    int offset = 0;
    
    // Determine which half of the buffer to process
    if (process_audio_half) {
        offset = 0;
        process_audio_half = 0;
    } else if (process_audio_full) {
        offset = AUDIO_CHUNK_SIZE;
        process_audio_full = 0;
    } else {
        return; // Nothing to process
    }

    // Process AUDIO_CHUNK_SIZE samples (L,R interleaved)
    for (int i = 0; i < AUDIO_CHUNK_SIZE; i += 2)
    {
        int buf_idx = offset + i;
        
        // 1. Get Bluetooth Music (16-bit interleaved)
        int16_t bt_l = uart_rx_buffer[buf_idx];
        int16_t bt_r = uart_rx_buffer[buf_idx + 1];

        // 2. Get INMP441 Mic (32-bit slot, data in top 24 bits)
        // Extract 16-bit MSB from the 32-bit I2S frame
        int32_t mic_raw = mic_rx_buffer[buf_idx]; 
        int16_t mic = (int16_t)(mic_raw >> 16); 

        int32_t out_l, out_r;

        if (is_anc_on) {
            // ANC Mode: invert mic and mix with delay
            int16_t delayed_mic = delay_buffer[delay_index];
            delay_buffer[delay_index] = mic;
            delay_index = (delay_index + 1) % 64;

            int32_t anc_signal = (int32_t)(-delayed_mic * ANC_GAIN);

            out_l = bt_l + anc_signal;
            out_r = bt_r + anc_signal;
        } else {
            // Transparency Mode: mix mic directly
            int32_t trans_signal = (int32_t)(mic * TRANSPARENCY_GAIN);
            
            out_l = bt_l + trans_signal;
            out_r = bt_r + trans_signal;
        }

        // Soft clip
        if (out_l > 32767) out_l = 32767;
        if (out_l < -32768) out_l = -32768;
        if (out_r > 32767) out_r = 32767;
        if (out_r < -32768) out_r = -32768;

        // Output to DAC
        dac_tx_buffer[buf_idx] = (int16_t)out_l;
        dac_tx_buffer[buf_idx + 1] = (int16_t)out_r;
    }
}
