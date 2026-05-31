#ifndef AUDIO_PIPELINE_H
#define AUDIO_PIPELINE_H

#include <stdint.h>
#include "stm32f4xx_hal.h"
#include "ring_buffer.h"

// 256 int16_t samples = 128 stereo frames (Left, Right)
#define AUDIO_CHUNK_SIZE 256

#define MIC_DMA_BUFFER_SIZE (AUDIO_CHUNK_SIZE * 4) // 32-bit L/R interleaved * 2 (half/full)
#define AMP_DMA_BUFFER_SIZE (AUDIO_CHUNK_SIZE * 4) // 16-bit L/R interleaved * 2 (half/full)

extern int32_t mic_rx_buffer[MIC_DMA_BUFFER_SIZE];
extern int16_t amp_tx_buffer[AMP_DMA_BUFFER_SIZE];

extern RingBuffer_t Mic_RingBuffer;
extern RingBuffer_t Amp_RingBuffer;

void AudioPipeline_Init(void);
void AudioPipeline_Start(void);

#endif // AUDIO_PIPELINE_H
