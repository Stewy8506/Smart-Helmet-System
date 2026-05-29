/**
 * @file audio_pipeline.h
 * @brief Hardware initialization and DMA management for the audio pipeline.
 * 
 * This module handles setting up the UART for receiving Bluetooth audio,
 * I2S2 for receiving the microphone signal, and I2S3 for transmitting
 * to the DAC. It relies on circular DMA to transfer data with zero CPU overhead.
 */
#ifndef AUDIO_PIPELINE_H
#define AUDIO_PIPELINE_H

#include <stdint.h>
#include "stm32f4xx_hal.h"

// 256 int16_t samples = 128 stereo frames (Left, Right)
#define AUDIO_CHUNK_SIZE 256

// Double buffer for DMA (Half-transfer and Full-transfer)
#define DMA_BUFFER_SIZE  (AUDIO_CHUNK_SIZE * 2)

extern int16_t uart_rx_buffer[DMA_BUFFER_SIZE];
extern int32_t mic_rx_buffer[DMA_BUFFER_SIZE];
extern int16_t dac_tx_buffer[DMA_BUFFER_SIZE];

extern volatile uint8_t process_audio_half;
extern volatile uint8_t process_audio_full;

void AudioPipeline_Init(void);
void AudioPipeline_Start(void);

#endif // AUDIO_PIPELINE_H
