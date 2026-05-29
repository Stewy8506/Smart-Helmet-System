#include "audio_pipeline.h"
#include "pin_config.h"
#include "uart_parser.h"
#include "main.h"
#include <string.h>

int32_t mic_rx_buffer[DMA_BUFFER_SIZE];
int16_t dac_tx_buffer[DMA_BUFFER_SIZE];

RingBuffer_t Mic_RingBuffer;
RingBuffer_t Dac_RingBuffer;

UART_HandleTypeDef huart1;
I2S_HandleTypeDef hi2s2;
I2S_HandleTypeDef hi2s3;

DMA_HandleTypeDef hdma_usart1_rx;
DMA_HandleTypeDef hdma_spi2_rx;
DMA_HandleTypeDef hdma_spi3_tx;

static void MX_DMA_Init(void)
{
    __HAL_RCC_DMA2_CLK_ENABLE();
    __HAL_RCC_DMA1_CLK_ENABLE();

    // DMA2 Stream 2 for USART1 RX
    HAL_NVIC_SetPriority(DMA2_Stream2_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(DMA2_Stream2_IRQn);

    // DMA1 Stream 3 for SPI2_RX
    HAL_NVIC_SetPriority(DMA1_Stream3_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(DMA1_Stream3_IRQn);

    // DMA1 Stream 5 for SPI3_TX
    HAL_NVIC_SetPriority(DMA1_Stream5_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(DMA1_Stream5_IRQn);
}

static void MX_USART1_UART_Init(void)
{
    huart1.Instance = BT_UART;
    huart1.Init.BaudRate = 2000000;
    huart1.Init.WordLength = UART_WORDLENGTH_8B;
    huart1.Init.StopBits = UART_STOPBITS_1;
    huart1.Init.Parity = UART_PARITY_NONE;
    huart1.Init.Mode = UART_MODE_RX; // We only need RX
    huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&huart1) != HAL_OK)
    {
        Error_Handler();
    }
}

static void MX_I2S2_Init(uint32_t audio_freq)
{
    hi2s2.Instance = MIC_I2S;
    hi2s2.Init.Mode = I2S_MODE_MASTER_RX;
    hi2s2.Init.Standard = I2S_STANDARD_PHILIPS;
    hi2s2.Init.DataFormat = I2S_DATAFORMAT_32B;
    hi2s2.Init.MCLKOutput = I2S_MCLKOUTPUT_DISABLE;
    hi2s2.Init.AudioFreq = audio_freq;
    hi2s2.Init.CPOL = I2S_CPOL_LOW;
    hi2s2.Init.ClockSource = I2S_CLOCK_PLL;
    hi2s2.Init.FullDuplexMode = I2S_FULLDUPLEXMODE_DISABLE;
    if (HAL_I2S_Init(&hi2s2) != HAL_OK)
    {
        Error_Handler();
    }
}

static void MX_I2S3_Init(uint32_t audio_freq)
{
    hi2s3.Instance = DAC_I2S;
    hi2s3.Init.Mode = I2S_MODE_MASTER_TX;
    hi2s3.Init.Standard = I2S_STANDARD_PHILIPS;
    hi2s3.Init.DataFormat = I2S_DATAFORMAT_16B;
    hi2s3.Init.MCLKOutput = I2S_MCLKOUTPUT_DISABLE;
    hi2s3.Init.AudioFreq = audio_freq;
    hi2s3.Init.CPOL = I2S_CPOL_LOW;
    hi2s3.Init.ClockSource = I2S_CLOCK_PLL;
    hi2s3.Init.FullDuplexMode = I2S_FULLDUPLEXMODE_DISABLE;
    if (HAL_I2S_Init(&hi2s3) != HAL_OK)
    {
        Error_Handler();
    }
}

void HAL_UART_MspInit(UART_HandleTypeDef* uartHandle)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    if(uartHandle->Instance==BT_UART)
    {
        __HAL_RCC_USART1_CLK_ENABLE();
        __HAL_RCC_GPIOA_CLK_ENABLE();
        
        GPIO_InitStruct.Pin = BT_UART_RX_PIN;
        GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
        GPIO_InitStruct.Alternate = BT_UART_AF;
        HAL_GPIO_Init(BT_UART_PORT, &GPIO_InitStruct);

        hdma_usart1_rx.Instance = DMA2_Stream2;
        hdma_usart1_rx.Init.Channel = DMA_CHANNEL_4;
        hdma_usart1_rx.Init.Direction = DMA_PERIPH_TO_MEMORY;
        hdma_usart1_rx.Init.PeriphInc = DMA_PINC_DISABLE;
        hdma_usart1_rx.Init.MemInc = DMA_MINC_ENABLE;
        hdma_usart1_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
        hdma_usart1_rx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
        hdma_usart1_rx.Init.Mode = DMA_CIRCULAR;
        hdma_usart1_rx.Init.Priority = DMA_PRIORITY_MEDIUM;
        hdma_usart1_rx.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
        if (HAL_DMA_Init(&hdma_usart1_rx) != HAL_OK)
        {
            Error_Handler();
        }
        __HAL_LINKDMA(uartHandle,hdmarx,hdma_usart1_rx);
    }
}

void HAL_I2S_MspInit(I2S_HandleTypeDef* i2sHandle)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    if(i2sHandle->Instance==MIC_I2S)
    {
        __HAL_RCC_SPI2_CLK_ENABLE();
        __HAL_RCC_GPIOB_CLK_ENABLE();
        
        GPIO_InitStruct.Pin = MIC_WS_PIN|MIC_SCK_PIN|MIC_SD_PIN;
        GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
        GPIO_InitStruct.Alternate = MIC_I2S_AF;
        HAL_GPIO_Init(MIC_I2S_PORT, &GPIO_InitStruct);

        hdma_spi2_rx.Instance = DMA1_Stream3;
        hdma_spi2_rx.Init.Channel = DMA_CHANNEL_0;
        hdma_spi2_rx.Init.Direction = DMA_PERIPH_TO_MEMORY;
        hdma_spi2_rx.Init.PeriphInc = DMA_PINC_DISABLE;
        hdma_spi2_rx.Init.MemInc = DMA_MINC_ENABLE;
        hdma_spi2_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
        hdma_spi2_rx.Init.MemDataAlignment = DMA_MDATAALIGN_WORD;
        hdma_spi2_rx.Init.Mode = DMA_CIRCULAR;
        hdma_spi2_rx.Init.Priority = DMA_PRIORITY_HIGH;
        hdma_spi2_rx.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
        if (HAL_DMA_Init(&hdma_spi2_rx) != HAL_OK)
        {
            Error_Handler();
        }
        __HAL_LINKDMA(i2sHandle,hdmarx,hdma_spi2_rx);
    }
    else if(i2sHandle->Instance==DAC_I2S)
    {
        __HAL_RCC_SPI3_CLK_ENABLE();
        __HAL_RCC_GPIOA_CLK_ENABLE();
        __HAL_RCC_GPIOB_CLK_ENABLE();
        
        GPIO_InitStruct.Pin = DAC_WS_PIN;
        GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
        GPIO_InitStruct.Alternate = DAC_I2S_AF;
        HAL_GPIO_Init(DAC_WS_PORT, &GPIO_InitStruct);

        GPIO_InitStruct.Pin = DAC_SCK_PIN|DAC_SD_PIN;
        HAL_GPIO_Init(DAC_I2S_PORT, &GPIO_InitStruct);

        hdma_spi3_tx.Instance = DMA1_Stream5;
        hdma_spi3_tx.Init.Channel = DMA_CHANNEL_0;
        hdma_spi3_tx.Init.Direction = DMA_MEMORY_TO_PERIPH;
        hdma_spi3_tx.Init.PeriphInc = DMA_PINC_DISABLE;
        hdma_spi3_tx.Init.MemInc = DMA_MINC_ENABLE;
        hdma_spi3_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
        hdma_spi3_tx.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
        hdma_spi3_tx.Init.Mode = DMA_CIRCULAR;
        hdma_spi3_tx.Init.Priority = DMA_PRIORITY_VERY_HIGH;
        hdma_spi3_tx.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
        if (HAL_DMA_Init(&hdma_spi3_tx) != HAL_OK)
        {
            Error_Handler();
        }
        __HAL_LINKDMA(i2sHandle,hdmatx,hdma_spi3_tx);
    }
}

void AudioPipeline_Init(void)
{
    memset(mic_rx_buffer, 0, sizeof(mic_rx_buffer));
    memset(dac_tx_buffer, 0, sizeof(dac_tx_buffer));

    RingBuffer_Init(&Mic_RingBuffer);
    RingBuffer_Init(&Dac_RingBuffer);

    UartParser_Init();

    MX_DMA_Init();
    MX_USART1_UART_Init();
    MX_I2S2_Init(I2S_AUDIOFREQ_44K); // Default
    MX_I2S3_Init(I2S_AUDIOFREQ_44K);
}

void AudioPipeline_Start(void)
{
    HAL_UART_Receive_DMA(&huart1, uart_dma_buffer, 4096); 
    HAL_I2S_Receive_DMA(&hi2s2, (uint16_t*)mic_rx_buffer, DMA_BUFFER_SIZE * 2);
    HAL_I2S_Transmit_DMA(&hi2s3, (uint16_t*)dac_tx_buffer, DMA_BUFFER_SIZE);
}

void AudioPipeline_SetSampleRate(uint32_t sample_rate)
{
    uint32_t i2s_freq = I2S_AUDIOFREQ_44K;
    if (sample_rate == 48000) {
        i2s_freq = I2S_AUDIOFREQ_48K;
    }

    // Stop streams
    HAL_I2S_DMAStop(&hi2s2);
    HAL_I2S_DMAStop(&hi2s3);

    // Re-init with new freq
    HAL_I2S_DeInit(&hi2s2);
    HAL_I2S_DeInit(&hi2s3);
    
    MX_I2S2_Init(i2s_freq);
    MX_I2S3_Init(i2s_freq);

    // Clear buffers
    RingBuffer_Clear(&Mic_RingBuffer);
    RingBuffer_Clear(&Dac_RingBuffer);

    // Notify ANC engine (TODO: switch coefficients)
    // extern void ANC_SetSampleRate(uint32_t sr);
    // ANC_SetSampleRate(sample_rate);

    // Restart
    HAL_I2S_Receive_DMA(&hi2s2, (uint16_t*)mic_rx_buffer, DMA_BUFFER_SIZE * 2);
    HAL_I2S_Transmit_DMA(&hi2s3, (uint16_t*)dac_tx_buffer, DMA_BUFFER_SIZE);
}

static void ProcessMicData(int startIndex) {
    int16_t extracted[AUDIO_CHUNK_SIZE];
    for (int i = 0; i < AUDIO_CHUNK_SIZE; i++) {
        // Extract 16-bit MSB from 32-bit I2S data
        extracted[i] = (int16_t)(mic_rx_buffer[startIndex + i] >> 16);
    }
    RingBuffer_Write(&Mic_RingBuffer, extracted, AUDIO_CHUNK_SIZE);
}

void HAL_I2S_RxHalfCpltCallback(I2S_HandleTypeDef *hi2s) {
    if (hi2s->Instance == MIC_I2S) {
        ProcessMicData(0);
    }
}

void HAL_I2S_RxCpltCallback(I2S_HandleTypeDef *hi2s) {
    if (hi2s->Instance == MIC_I2S) {
        ProcessMicData(AUDIO_CHUNK_SIZE);
    }
}

static void ProcessDacData(int startIndex) {
    if (RingBuffer_GetCount(&Dac_RingBuffer) >= AUDIO_CHUNK_SIZE) {
        RingBuffer_Read(&Dac_RingBuffer, &dac_tx_buffer[startIndex], AUDIO_CHUNK_SIZE);
    } else {
        // Underflow: Output silence
        memset(&dac_tx_buffer[startIndex], 0, AUDIO_CHUNK_SIZE * sizeof(int16_t));
    }
}

void HAL_I2S_TxHalfCpltCallback(I2S_HandleTypeDef *hi2s)
{
    if (hi2s->Instance == DAC_I2S) {
        ProcessDacData(0);
    }
}

void HAL_I2S_TxCpltCallback(I2S_HandleTypeDef *hi2s)
{
    if (hi2s->Instance == DAC_I2S) {
        ProcessDacData(AUDIO_CHUNK_SIZE);
    }
}

// DMA IRQ Handlers
void DMA2_Stream2_IRQHandler(void) { HAL_DMA_IRQHandler(&hdma_usart1_rx); }
void DMA1_Stream3_IRQHandler(void) { HAL_DMA_IRQHandler(&hdma_spi2_rx); }
void DMA1_Stream5_IRQHandler(void) { HAL_DMA_IRQHandler(&hdma_spi3_tx); }
