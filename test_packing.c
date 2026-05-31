#include <stdio.h>
#include <stdint.h>

int main() {
    int16_t sample1 = 32767;
    int16_t sample2 = -32768;
    int16_t sample3 = -1;
    
    int32_t out1 = (sample1 & 0xFFFF);
    int32_t out2 = (sample2 & 0xFFFF);
    int32_t out3 = (sample3 & 0xFFFF);
    
    // Simulate STM32 Little Endian memory and DMA
    // Address 0 (lower 16) goes to MSB, Address 2 (upper 16) goes to LSB
    uint16_t msb1 = out1 & 0xFFFF;
    uint16_t lsb1 = (out1 >> 16) & 0xFFFF;
    int32_t received1 = (msb1 << 16) | lsb1;
    
    uint16_t msb2 = out2 & 0xFFFF;
    uint16_t lsb2 = (out2 >> 16) & 0xFFFF;
    int32_t received2 = (msb2 << 16) | lsb2;
    
    uint16_t msb3 = out3 & 0xFFFF;
    uint16_t lsb3 = (out3 >> 16) & 0xFFFF;
    int32_t received3 = (msb3 << 16) | lsb3;
    
    printf("16-bit: %d -> 32-bit received: %d\n", sample1, received1);
    printf("16-bit: %d -> 32-bit received: %d\n", sample2, received2);
    printf("16-bit: %d -> 32-bit received: %d\n", sample3, received3);
    
    return 0;
}
