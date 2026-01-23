#include "pico/stdlib.h"
#include <stdio.h>
#include <inttypes.h>
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "video.pio.h"
#include "hardware/clocks.h"

#define HSYNC_PIN 5

#define DAC0 5
#define DAC1 6
#define DAC2 7

#define UART_ID uart0
#define BAUD_RATE 921600

uint8_t screen_data[4096];


// Test data to stream
uint8_t test_data[] = {
    0xFF, 0xFF, 0xFF, 0xFF
};


int dma_chan;

void dma_handler() {
    dma_hw->ints0 = 1u << dma_chan;

    dma_channel_set_read_addr(
        dma_chan,
        test_data,
        true
    );
}


int main() {
    set_sys_clock_khz(125000, true);
    stdio_init_all();
    uart_init(UART_ID, BAUD_RATE);

    // Allow time for the USB uart to connect
    sleep_ms(3000);

    printf("sys clk = %lu Hz\n", clock_get_hz(clk_sys));

    // Use PIO0 for capture of video data
    PIO pio = pio0;

    // -------------------------------
    // Load all programs in PIO memory
    // -------------------------------
    uint offset_hsync = pio_add_program(pio, &hsync_program);
    uint offset_line = pio_add_program(pio, &line_program);
    uint offset_vsync = pio_add_program(pio, &vsync_program);

    // -------------------------------
    // Allocate state machines
    // -------------------------------
    uint sm_hsync   = pio_claim_unused_sm(pio, true);
    uint sm_line   = pio_claim_unused_sm(pio, true);
    uint sm_vsync   = pio_claim_unused_sm(pio, true);

    // -------------------------------
    // hsync program
    // -------------------------------
    pio_gpio_init(pio, DAC0);
    pio_gpio_init(pio, DAC1);
    pio_gpio_init(pio, DAC2);
    gpio_set_dir(DAC0, GPIO_OUT);
    gpio_set_dir(DAC1, GPIO_OUT);
    gpio_set_dir(DAC2, GPIO_OUT);

    hsync_program_init(pio, sm_hsync, offset_hsync, DAC0);
    line_program_init(pio, sm_line, offset_line, DAC0);
    vsync_program_init(pio, sm_vsync, offset_vsync, DAC0);





    // Configure a channel to write the same word (32 bits) repeatedly to PIO0
    // SM0's TX FIFO, paced by the data request signal from that peripheral.
    dma_chan = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config(dma_chan);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_dreq(&c, pio_get_dreq(pio, sm_hsync, true)); // NS
    channel_config_set_chain_to(&c, dma_chan);

    dma_channel_configure(
        dma_chan,
        &c,
        &pio0_hw->txf[sm_hsync], // Write address (only need to set this once)
        test_data,             // Don't provide a read address yet
        sizeof(test_data) / 4,                  // Write the same value many times, then halt and interrupt
        true             // Don't start yet
    );

    // Tell the DMA to raise IRQ line 0 when the channel finishes a block
    dma_channel_set_irq0_enabled(dma_chan, true);

    // Configure the processor to run dma_handler() when DMA IRQ 0 is asserted
    irq_set_exclusive_handler(DMA_IRQ_0, dma_handler);
    irq_set_enabled(DMA_IRQ_0, true);


    printf("Running");


    while(true) {}

    // pio_remove_program_and_unclaim_sm(&vsync_program, pio, sm_vsync, offset_vsync);
    // pio_remove_program_and_unclaim_sm(&raster_program, pio, sm_raster, offset_raster);
    // pio_remove_program_and_unclaim_sm(&capture_program, pio, sm_capture, offset_capture);

}