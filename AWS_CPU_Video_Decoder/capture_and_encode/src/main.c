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

// USB UART Settings
#define UART_ID uart0
#define BAUD_RATE 921600

// capture buffer TODO: replace
// uint16_t screen_data[80 * 29];

extern uint16_t screen_data[SCREEN_HEIGHT * SCREEN_WIDTH];

uint16_t *screen_data_offset = screen_data + 80 * 5;

int dma_capture;

// TODO: Rename
int dma_chan0;
int dma_chan1;


// Video line buffers
uint8_t line_a[LINE_SIZE];
uint8_t line_b[LINE_SIZE];
volatile uint8_t active_buffer_index = 0;
uint8_t *line_buffers[] = { line_a, line_b };

uint8_t *active_line; // This is the line that is  being sent out the PIO via DMA
uint8_t *inactive_line; // This is the line that is  being sent out the PIO via DMA


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


void dma1_irq_handler() {
    // Clear DMA1 interrupt
    dma_hw->ints0 = 1u << dma_chan1;

    // Switch active buffer
    active_buffer_index ^= 1;
    active_line = line_buffers[active_buffer_index];
    inactive_line = line_buffers[active_buffer_index];

    write_scanline(inactive_line);
}



int main() {
    set_sys_clock_khz(125000, true);
    stdio_init_all();
    uart_init(UART_ID, BAUD_RATE);

    // Allow time for the USB uart to connect
    // sleep_ms(3000);

    // Setup video
    video_init();
    // fill_screen_with_character(0x05, 0);
    active_line = line_buffers[active_buffer_index];
    inactive_line = line_buffers[active_buffer_index ^ 1];

    printf("Running video sample");

    PIO pio_zero = pio0; // Use PIO0 for capture of video data
    PIO pio_one = pio1;  // Use PIO1 for video

    // -------------------------------
    // Load all programs in PIO memory
    // -------------------------------
    uint offset_vsync = pio_add_program(pio_zero, &vsync_program);
    uint offset_raster = pio_add_program(pio_zero, &raster_program);
    uint offset_capture = pio_add_program(pio_zero, &capture_program);

    uint offset_dma = pio_add_program(pio_one, &dma_program);

    // -------------------------------
    // Allocate state machines
    // -------------------------------
    uint sm_vsync   = pio_claim_unused_sm(pio_zero, true);
    uint sm_raster  = pio_claim_unused_sm(pio_zero, true);
    uint sm_capture = pio_claim_unused_sm(pio_zero, true);

    uint sm_dma = pio_claim_unused_sm(pio_one, true);

    // -------------------------------
    // CHARACTER_CAPTURE program
    // -------------------------------
    pio_gpio_init(pio_zero, CCLK_PIN);
    gpio_set_dir(CCLK_PIN, GPIO_IN);

    for(int cc_pin = CCODE_PINS; cc_pin > CCODE_PINS + 8; cc_pin++) {
        pio_gpio_init(pio_zero, cc_pin);
    }
    capture_program_init(pio_zero, sm_capture, offset_capture, CCODE_PINS);

    // -------------------------------
    // RASTER_ROW program
    // -------------------------------
    pio_gpio_init(pio_zero, HSYNC_PIN);
    gpio_set_dir(HSYNC_PIN, GPIO_IN);
    raster_program_init(pio_zero, sm_raster, offset_capture, HSYNC_PIN);

    // -------------------------------
    // VSYNC program
    // -------------------------------
    pio_gpio_init(pio_zero, VSYNC_PIN);      // enable input into PIO
    gpio_set_dir(VSYNC_PIN, GPIO_IN);
    vsync_program_init(pio_zero, sm_vsync, offset_vsync, VSYNC_PIN);

    // -------------------------------
    // VIDEO program
    // -------------------------------
    dma_program_init(pio_one, sm_dma, offset_dma, DAC0);

    // Stop the sm
    pio_sm_set_enabled(pio_zero, sm_capture, false);
    pio_sm_set_enabled(pio_zero, sm_raster, false);
    pio_sm_set_enabled(pio_zero, sm_vsync, false);

    // Set up DMA for capture
    dma_capture = dma_claim_unused_channel(true);
    dma_channel_config dma_capture_cf = dma_channel_get_default_config(dma_capture);

    channel_config_set_transfer_data_size(&dma_capture_cf, DMA_SIZE_16);  // Copy one full word
    channel_config_set_read_increment(&dma_capture_cf, false); // Read from the PIO output buffer
    channel_config_set_write_increment(&dma_capture_cf, true); // Write to RAM
    channel_config_set_dreq(&dma_capture_cf, pio_get_dreq(pio_zero, sm_capture, false));

    // Set DMA for video capture
    dma_channel_configure(
        dma_capture,
        &dma_capture_cf,
        screen_data_offset,                // Write address
        &pio_zero->rxf[sm_capture], // Read address (the address of the pio's fifo)
        80 * 29,                    // Number of transfers
        false                       // Autostart
    );

    // Tirgger a irq at the end of the read
    dma_channel_set_irq0_enabled(dma_capture, true);
    irq_set_exclusive_handler(DMA_IRQ_0, dma_capture_irq_handler);
    irq_set_enabled(DMA_IRQ_0, true);



    // Setup DMA for video
    dma_chan0 = dma_claim_unused_channel(true);
    dma_chan1 = dma_claim_unused_channel(true);

    dma_channel_config c0 = dma_channel_get_default_config(dma_chan0);

    channel_config_set_transfer_data_size(&c0, DMA_SIZE_8);  // One byte at a time
    channel_config_set_read_increment(&c0, true);
    channel_config_set_write_increment(&c0, false);
    channel_config_set_dreq(&c0, pio_get_dreq(pio_one, sm_dma, true)); // Send byte as pio needs it
    channel_config_set_chain_to(&c0, dma_chan1); // On the end of DMA0 start DMA1

    // Set up DMA0
    dma_channel_configure(
        dma_chan0,
        &c0,
        &pio_one->txf[sm_dma], // Write address (the address of the pio's fifo)
        line_blank_field,      // Address of the data to be read
        1000,                 // Number of transfers
        false                 // Autostart
    );

    dma_channel_config c1 = dma_channel_get_default_config(dma_chan1);
    channel_config_set_transfer_data_size(&c1, DMA_SIZE_32);    // Size of a pointer
    channel_config_set_read_increment(&c1, false);              // Read the same address
    channel_config_set_write_increment(&c1, false);             // Write the same address
    channel_config_set_chain_to(&c1, dma_chan0);                // On the end of DMA1 start DMA0

    dma_channel_configure(
        dma_chan1,
        &c1,
        &dma_hw->ch[dma_chan0].read_addr, // Write to the "source address" for DMA0
        &active_line,
        1, // Move just the pointer
        false
    );

    // Tirgger a irq at the end of the "reset" transfer
    dma_channel_set_irq1_enabled(dma_chan1, true);
    irq_set_exclusive_handler(DMA_IRQ_1, dma1_irq_handler);
    irq_set_enabled(DMA_IRQ_1, true);

    // Start DMA
    dma_start_channel_mask(1u << dma_chan1);
    dma_start_channel_mask(1u << dma_capture);

    // Start PIO
    capture_program_init(pio_zero, sm_capture, offset_capture, CCODE_PINS);
    raster_program_init(pio_zero, sm_raster, offset_raster, HSYNC_PIN);
    vsync_program_init(pio_zero, sm_vsync, offset_vsync, VSYNC_PIN);

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
            for(uint j = 0; j < 80; j++) {

                uint index = (i * 80) + j;

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
    pio_remove_program_and_unclaim_sm(&vsync_program, pio_zero, sm_vsync, offset_vsync);
    pio_remove_program_and_unclaim_sm(&raster_program, pio_zero, sm_raster, offset_raster);
    pio_remove_program_and_unclaim_sm(&capture_program, pio_zero, sm_capture, offset_capture);

}