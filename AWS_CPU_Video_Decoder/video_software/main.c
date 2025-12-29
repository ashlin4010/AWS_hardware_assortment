#include "pico/stdlib.h"
#include <stdio.h>
#include <inttypes.h>
#include "hardware/pio.h"
#include "decode.pio.h"
#include "hardware/clocks.h"

#define VSYNC_PIN 2
#define HSYNC_PIN 3
#define CCODE_PINS 4 // 4-11
#define CCLK_PIN 12

uint8_t screen_data[4096];


int main() {
    set_sys_clock_khz(125000, true);
    stdio_init_all();

    // Allow time for the USB uart to connect
    sleep_ms(3000);

    // Use PIO0 for capture of video data
    PIO pio = pio0;

    // -------------------------------
    // Load all programs in PIO memory
    // -------------------------------
    uint offset_vsync = pio_add_program(pio, &vsync_program);
    uint offset_raster = pio_add_program(pio, &raster_program);
    uint offset_capture = pio_add_program(pio, &capture_program);

    // -------------------------------
    // Allocate state machines
    // -------------------------------
    uint sm_vsync   = pio_claim_unused_sm(pio, true);
    uint sm_raster  = pio_claim_unused_sm(pio, true);
    uint sm_capture = pio_claim_unused_sm(pio, true);

    // -------------------------------
    // CHARACTER_CAPTURE program
    // -------------------------------
    pio_gpio_init(pio, CCLK_PIN);
    gpio_set_dir(CCLK_PIN, GPIO_IN);

    for(int cc_pin = CCODE_PINS; cc_pin > CCODE_PINS + 8; cc_pin++) {
        pio_gpio_init(pio, cc_pin);
        gpio_set_dir(cc_pin, GPIO_IN);
    }
    capture_program_init(pio, sm_capture, offset_capture, CCODE_PINS);

    // -------------------------------
    // RASTER_ROW program
    // -------------------------------
    pio_gpio_init(pio, HSYNC_PIN);
    gpio_set_dir(HSYNC_PIN, GPIO_IN);
    raster_program_init(pio, sm_raster, offset_capture, HSYNC_PIN);

    // -------------------------------
    // VSYNC program
    // -------------------------------
    pio_gpio_init(pio, VSYNC_PIN);      // enable input into PIO
    gpio_set_dir(VSYNC_PIN, GPIO_IN);
    vsync_program_init(pio, sm_vsync, offset_vsync, VSYNC_PIN);

    // Stop the sm
    pio_sm_set_enabled(pio, sm_capture, false);
    pio_sm_set_enabled(pio, sm_raster, false);
    pio_sm_set_enabled(pio, sm_vsync, false);

    printf("\033[?25l");  // hide cursor

    
    while (1) {
        uint c_count = 0;
        uint8_t *p = screen_data;

        capture_program_init(pio, sm_capture, offset_capture, CCODE_PINS);
        raster_program_init(pio, sm_raster, offset_raster, HSYNC_PIN);
        vsync_program_init(pio, sm_vsync, offset_vsync, VSYNC_PIN);

        while (c_count < 2240) {
            uint8_t byte = 0xFF & pio_sm_get_blocking(pio, sm_capture);
            c_count++;
            *p = byte;
            p++;
        }

        // Stop sm
        pio_sm_set_enabled(pio, sm_capture, false);
        pio_sm_set_enabled(pio, sm_raster, false);
        pio_sm_set_enabled(pio, sm_vsync, false);

        printf("\033[H"); // just move to top-left
        printf("Done, count: %d\n", c_count);

        uint index = 0;
        for(uint i = 0; i < 28; i++) {
            for(uint j = 0; j < 80; j++) {
                printf("%c", (char)screen_data[index]);
                index++;
            }
            printf("\n");
        }
    }

    while(true) {}

    pio_remove_program_and_unclaim_sm(&vsync_program, pio, sm_vsync, offset_vsync);
    pio_remove_program_and_unclaim_sm(&raster_program, pio, sm_raster, offset_raster);
    pio_remove_program_and_unclaim_sm(&capture_program, pio, sm_capture, offset_capture);

}