#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "pico/stdlib.h"
#include "hardware/dma.h"
#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "decode.pio.h"
#include "video.pio.h"
#include "font/font.h"
#include "video.h"

// Capture pins
#define VSYNC_PIN 2
#define HSYNC_PIN 3
#define CCODE_PINS 4 // 4-11
#define CCLK_PIN 12

// Video output pins
#define DAC0 20
#define DAC1 21
#define DAC2 22

#define AWS_SCREEN_WIDTH 80
#define AWS_SCREEN_HEIGHT 29

// USB UART Settings
#define UART_ID uart0
#define BAUD_RATE 921600

extern uint16_t screen_data[SCREEN_HEIGHT * SCREEN_WIDTH];
uint16_t *screen_data_offset = screen_data + SCREEN_WIDTH * 0; // Allow video capture to be offset

int dma_capture;
int dma_video;
int dma_restart;

// Video line buffers
volatile uint8_t active_buffer_index = 0;
uint8_t line_buffers[2][LINE_SIZE];
uint8_t *active_line = line_buffers[0];
uint8_t *inactive_line = line_buffers[0];

// Called at the end of ever frame capture, re-trigger DMA
void dma_capture_irq_handler() {
    // Clear interrupt
    dma_hw->ints0 = 1u << dma_capture;

    // Reload DMA
    dma_channel_set_write_addr(
        dma_capture,
        screen_data_offset,
        true
    );
}

// Called just before starting the next scanline
void dma_video_restart_irq_handler() {
    // Clear interrupt
    dma_hw->ints0 = 1u << dma_restart;

    // Switch active line buffer
    active_buffer_index ^= 1;
    active_line = line_buffers[active_buffer_index];
    inactive_line = line_buffers[active_buffer_index];

    // Build the next scan line
    write_scanline(inactive_line);
}

int main() {
    set_sys_clock_khz(125000, true);
    stdio_init_all();
    uart_init(UART_ID, BAUD_RATE);

    // Setup video
    video_init();
    // fill_screen_with_character(0x05, 0);
    fill_screen_with_character(0, 0);

    PIO pio_capture = pio0; // Use PIO0 for capture of video data
    PIO pio_video = pio1;  // Use PIO1 for video

    // -------------------------------
    // Load All Programs in PIO memory
    // -------------------------------
    uint offset_capture = pio_add_program(pio_capture, &capture_program);
    uint offset_raster  = pio_add_program(pio_capture, &raster_program);
    uint offset_vsync   = pio_add_program(pio_capture, &vsync_program);
    uint offset_video   = pio_add_program(pio_video, &video_program);

    // -------------------------------
    // Allocate State Machines
    // -------------------------------
    uint sm_capture = pio_claim_unused_sm(pio_capture, true);
    uint sm_raster  = pio_claim_unused_sm(pio_capture, true);
    uint sm_vsync   = pio_claim_unused_sm(pio_capture, true);
    uint sm_video   = pio_claim_unused_sm(pio_video, true);

    // -------------------------------
    // PIO Program Init
    // -------------------------------
    capture_program_init(pio_capture, sm_capture, offset_capture, CCODE_PINS);
    raster_program_init(pio_capture, sm_raster, offset_capture, HSYNC_PIN);
    vsync_program_init(pio_capture, sm_vsync, offset_vsync, VSYNC_PIN);
    video_program_init(pio_video, sm_video, offset_video, DAC0);

    // Get DMA channels
    dma_capture = dma_claim_unused_channel(true);
    dma_video = dma_claim_unused_channel(true);
    dma_restart = dma_claim_unused_channel(true);

    // -------------------------------
    // DMA Capture
    // -------------------------------
    dma_channel_config dma_capture_cf = dma_channel_get_default_config(dma_capture);
    channel_config_set_transfer_data_size(&dma_capture_cf, DMA_SIZE_16);  // Copy one full word
    channel_config_set_read_increment(&dma_capture_cf, false); // Read from the PIO output buffer
    channel_config_set_write_increment(&dma_capture_cf, true); // Write to RAM
    channel_config_set_dreq(&dma_capture_cf, pio_get_dreq(pio_capture, sm_capture, false));

    dma_channel_configure(
        dma_capture,
        &dma_capture_cf,
        screen_data_offset,                     // Write address
        &pio_capture->rxf[sm_capture],          // Read address (the address of the pio's fifo)
        AWS_SCREEN_WIDTH * AWS_SCREEN_HEIGHT,   // Number of transfers
        false                                   // Autostart
    );

    // Tirgger a irq at the end of the read
    dma_channel_set_irq0_enabled(dma_capture, true);
    irq_set_exclusive_handler(DMA_IRQ_0, dma_capture_irq_handler);
    irq_set_enabled(DMA_IRQ_0, true);

    // -------------------------------
    // DMA Video Raster
    // -------------------------------
    dma_channel_config c0 = dma_channel_get_default_config(dma_video);
    channel_config_set_transfer_data_size(&c0, DMA_SIZE_8);  // One byte at a time
    channel_config_set_read_increment(&c0, true);
    channel_config_set_write_increment(&c0, false);
    channel_config_set_dreq(&c0, pio_get_dreq(pio_video, sm_video, true)); // Send byte as pio needs it
    channel_config_set_chain_to(&c0, dma_restart); // On the end of DMA0 start DMA1

    dma_channel_configure(
        dma_video,
        &c0,
        &pio_video->txf[sm_video],  // Write address (the address of the pio's fifo)
        line_blank_field,           // Address of the data to be read
        1000,                       // Number of transfers
        false                       // Autostart
    );

    // -------------------------------
    // DMA Video Chain
    // -------------------------------
    dma_channel_config c1 = dma_channel_get_default_config(dma_restart);
    channel_config_set_transfer_data_size(&c1, DMA_SIZE_32);    // Size of a pointer
    channel_config_set_read_increment(&c1, false);              // Read the same address
    channel_config_set_write_increment(&c1, false);             // Write the same address
    channel_config_set_chain_to(&c1, dma_video);                // On the end of DMA1 start DMA0

    dma_channel_configure(
        dma_restart,
        &c1,
        &dma_hw->ch[dma_video].read_addr,
        &active_line,
        1, // Move just the pointer
        false
    );

    // Tirgger an irq at the end of the "restart" transfer
    dma_channel_set_irq1_enabled(dma_restart, true);
    irq_set_exclusive_handler(DMA_IRQ_1, dma_video_restart_irq_handler);
    irq_set_enabled(DMA_IRQ_1, true);

    // -------------------------------
    // Start DMA and PIO
    // -------------------------------
    dma_start_channel_mask(1u << dma_restart); // Start video chain
    dma_start_channel_mask(1u << dma_capture); // Start capture

    capture_program_init(pio_capture, sm_capture, offset_capture, CCODE_PINS);
    raster_program_init(pio_capture, sm_raster, offset_raster, HSYNC_PIN);
    vsync_program_init(pio_capture, sm_vsync, offset_vsync, VSYNC_PIN);


    // Debug screen
    #if 0
    printf("\033[?25l");  // hide cursor
    while (1) {
        // Print capture
        printf("\033[H"); // just move to top-left
        putchar('\n');
        printf("+--------------------------------------------------------------------------------+\n");

        for(uint i = 0; i < 29; i++) {
            putchar('|');
            for(uint j = 0; j < SCREEN_WIDTH; j++) {
                uint index = (i * SCREEN_WIDTH) + j;
                uint16_t code = screen_data[index];
                uint8_t ac = 0x0F & (uint8_t)(code >> 8);
                uint8_t cc = (uint8_t)(code & 0xFF);

                if ((cc >= 32 && cc <= 126) || cc >= 128) {
                    putchar(cc);
                } else {
                    putchar(' ');
                }
            }
            putchar('|');
            printf("\n");
        }
        printf("+--------------------------------------------------------------------------------+\n\n");
    }
    #endif

    while (1) {}

    // Clean up
    pio_remove_program_and_unclaim_sm(&vsync_program, pio_capture, sm_vsync, offset_vsync);
    pio_remove_program_and_unclaim_sm(&raster_program, pio_capture, sm_raster, offset_raster);
    pio_remove_program_and_unclaim_sm(&capture_program, pio_capture, sm_capture, offset_capture);
    pio_remove_program_and_unclaim_sm(&video_program, pio_video, sm_video, offset_video);
}