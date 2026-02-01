#include "pico/stdlib.h"
#include <stdio.h>
#include <inttypes.h>
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "decode.pio.h"
#include "hardware/clocks.h"

#define VSYNC_PIN 2
#define HSYNC_PIN 3
#define CCODE_PINS 4 // 4-11
#define CCLK_PIN 12

#define UART_ID uart0
#define BAUD_RATE 921600

uint16_t screen_data[80 * 29];

int dma_capture;

void dma_capture_irq_handler() {
    // Clear interrupt
    dma_hw->ints0 = 1u << dma_capture;

    // Reload DMA
    dma_channel_set_write_addr(
        dma_capture,
        screen_data,
        true
    );
}

int main() {
    set_sys_clock_khz(125000, true);
    stdio_init_all();
    uart_init(UART_ID, BAUD_RATE);

    // Allow time for the USB uart to connect
    sleep_ms(3000);

    printf("Running video sample");
    // Use PIO0 for capture of video data
    PIO pio_zero = pio0;

    // -------------------------------
    // Load all programs in PIO memory
    // -------------------------------
    uint offset_vsync = pio_add_program(pio_zero, &vsync_program);
    uint offset_raster = pio_add_program(pio_zero, &raster_program);
    uint offset_capture = pio_add_program(pio_zero, &capture_program);

    // -------------------------------
    // Allocate state machines
    // -------------------------------
    uint sm_vsync   = pio_claim_unused_sm(pio_zero, true);
    uint sm_raster  = pio_claim_unused_sm(pio_zero, true);
    uint sm_capture = pio_claim_unused_sm(pio_zero, true);

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

    // Stop the sm
    pio_sm_set_enabled(pio_zero, sm_capture, false);
    pio_sm_set_enabled(pio_zero, sm_raster, false);
    pio_sm_set_enabled(pio_zero, sm_vsync, false);

    // Set up DMA
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
        screen_data,                // Write address
        &pio_zero->rxf[sm_capture], // Read address (the address of the pio's fifo)
        80 * 29,                    // Number of transfers
        false                       // Autostart
    );

    // Tirgger a irq at the end of the read
    dma_channel_set_irq0_enabled(dma_capture, true);
    irq_set_exclusive_handler(DMA_IRQ_0, dma_capture_irq_handler);
    irq_set_enabled(DMA_IRQ_0, true);

    printf("\033[?25l");  // hide cursor

    // Start DMA
    dma_start_channel_mask(1u << dma_capture);

    // Start PIO
    capture_program_init(pio_zero, sm_capture, offset_capture, CCODE_PINS);
    raster_program_init(pio_zero, sm_raster, offset_raster, HSYNC_PIN);
    vsync_program_init(pio_zero, sm_vsync, offset_vsync, VSYNC_PIN);

    while (1) {
        // Print capture
        printf("\033[H"); // just move to top-left
        putchar('\n');
        printf("+--------------------------------------------------------------------------------+\n");

        for(uint i = 0; i < 29; i++) {
            putchar('|');
            for(uint j = 0; j < 80; j++) {
                uint index = (i * 80) + j;
                unsigned char c = 0xff & screen_data[index];

                if ((c >= 32 && c <= 126) || c >= 128) {
                    putchar(c);
                } else {
                    putchar(' ');
                }
            }
            putchar('|');
            printf("\n");
        }
        printf("+--------------------------------------------------------------------------------+\n\n");
    }

    // Clean up
    pio_remove_program_and_unclaim_sm(&vsync_program, pio_zero, sm_vsync, offset_vsync);
    pio_remove_program_and_unclaim_sm(&raster_program, pio_zero, sm_raster, offset_raster);
    pio_remove_program_and_unclaim_sm(&capture_program, pio_zero, sm_capture, offset_capture);

}