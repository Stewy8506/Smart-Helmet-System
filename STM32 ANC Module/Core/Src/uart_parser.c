#include "uart_parser.h"
#include "audio_pipeline.h"
#include "stm32f4xx_hal.h"
#include <string.h>

#define UART_SYNC_WORD 0xAA55
#define UART_DMA_BUF_SIZE 4096

uint8_t uart_dma_buffer[UART_DMA_BUF_SIZE];
uint32_t uart_process_ptr = 0;

int16_t bt_audio_fifo[BT_FIFO_SIZE];
volatile uint32_t bt_fifo_head = 0;
volatile uint32_t bt_fifo_tail = 0;

extern DMA_HandleTypeDef hdma_usart1_rx;

void UartParser_Init(void) {
    memset(uart_dma_buffer, 0, sizeof(uart_dma_buffer));
    bt_fifo_head = 0;
    bt_fifo_tail = 0;
    uart_process_ptr = 0;
}

static void push_fifo(int16_t sample) {
    uint32_t next_head = (bt_fifo_head + 1) % BT_FIFO_SIZE;
    if (next_head != bt_fifo_tail) {
        bt_audio_fifo[bt_fifo_head] = sample;
        bt_fifo_head = next_head;
    }
}

// Called from main loop
void UartParser_Process(void) {
    // Determine how many bytes DMA has written
    uint32_t dma_curr_ndtr = __HAL_DMA_GET_COUNTER(&hdma_usart1_rx);
    uint32_t dma_write_ptr = UART_DMA_BUF_SIZE - dma_curr_ndtr;

    while (uart_process_ptr != dma_write_ptr) {
        uint32_t avail = (dma_write_ptr >= uart_process_ptr) ? 
                         (dma_write_ptr - uart_process_ptr) : 
                         (UART_DMA_BUF_SIZE - uart_process_ptr + dma_write_ptr);
                         
        if (avail < 4) break; // Not enough for header

        uint8_t b0 = uart_dma_buffer[uart_process_ptr];
        uint8_t b1 = uart_dma_buffer[(uart_process_ptr + 1) % UART_DMA_BUF_SIZE];
        uint16_t sync = b0 | (b1 << 8);

        if (sync == UART_SYNC_WORD) {
            uint8_t l0 = uart_dma_buffer[(uart_process_ptr + 2) % UART_DMA_BUF_SIZE];
            uint8_t l1 = uart_dma_buffer[(uart_process_ptr + 3) % UART_DMA_BUF_SIZE];
            uint16_t len = l0 | (l1 << 8); // length in bytes

            // Sanity check length
            if (len > 2048) {
                // Invalid length, probably lost sync
                uart_process_ptr = (uart_process_ptr + 1) % UART_DMA_BUF_SIZE;
                continue;
            }

            if (avail < 4 + len) {
                break; // wait for full packet
            }

            // Packet is complete, parse data
            for (int i = 0; i < len; i += 2) {
                uint8_t d0 = uart_dma_buffer[(uart_process_ptr + 4 + i) % UART_DMA_BUF_SIZE];
                uint8_t d1 = uart_dma_buffer[(uart_process_ptr + 4 + i + 1) % UART_DMA_BUF_SIZE];
                int16_t sample = (int16_t)(d0 | (d1 << 8));
                push_fifo(sample);
            }

            uart_process_ptr = (uart_process_ptr + 4 + len) % UART_DMA_BUF_SIZE;
        } else {
            uart_process_ptr = (uart_process_ptr + 1) % UART_DMA_BUF_SIZE;
        }
    }
}

int UartParser_ReadSamples(int16_t *out_buffer, int num_samples) {
    int count = 0;
    while (count < num_samples && bt_fifo_tail != bt_fifo_head) {
        out_buffer[count++] = bt_audio_fifo[bt_fifo_tail];
        bt_fifo_tail = (bt_fifo_tail + 1) % BT_FIFO_SIZE;
    }
    return count;
}
