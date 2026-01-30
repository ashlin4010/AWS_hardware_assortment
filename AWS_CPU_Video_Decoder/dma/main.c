#include "pico/stdlib.h"
#include <stdio.h>
#include <inttypes.h>
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "video.pio.h"
#include "hardware/clocks.h"
#include <string.h>

#define DAC0 20
#define DAC1 21
#define DAC2 22

#define UART_ID uart0
#define BAUD_RATE 921600

// double packed, two dots per byte
// We lose a tiny little bit of precision but half the memory requirements
// But it's computationally more expensive so we need to pre-calculated values we have a possible
#define VOLTS_0_DOUBLE 0x00
#define VOLTS_2_DOUBLE 0x24
#define VOLTS_3_DOUBLE 0x12
#define VOLTS_4_DOUBLE 0x09
#define VOLTS_5_DOUBLE 0x1B
#define VOLTS_6_DOUBLE 0x2D
#define VOLTS_7_DOUBLE 0x36
#define VOLTS_M_DOUBLE 0x3F

#define LINE_SIZE 1000

static uint32_t dma_dummy;

const uint LINE_DOTS = 1000;
const uint HALF_LINE_DOTS = LINE_DOTS / 2;

const uint HSYNC_DOTS = 74;
const uint BACK_PORCH_DOTS = 88;
const uint SCREEN_DATA_DOTS = 812;
const uint FRONT_PORCH_DOTS = 26;

const uint SHORT_SYNC_DOTS = 20;
const uint BROAD_SYNC_DOTS = 74;
const uint HSYNC_DOTS_OFFSET = 0;

const uint BACK_PORCH_DOTS_OFFSET = HSYNC_DOTS_OFFSET + HSYNC_DOTS;
const uint SCREEN_DATA_DOTS_OFFSET =  BACK_PORCH_DOTS_OFFSET + BACK_PORCH_DOTS;
const uint FRONT_PORCH_DOTS_OFFSET = SCREEN_DATA_DOTS_OFFSET + SCREEN_DATA_DOTS;
const uint SHORT_SYNC_DOTS_OFFSET = 0;
const uint BROAD_SYNC_DOTS_OFFSET = (LINE_DOTS / 2) - BROAD_SYNC_DOTS;

const uint screen_text_rows = 47;
const uint screen_text_columns = 80;


uint16_t screen_data[50 * 80] = {0};


uint8_t screen_row = 0;

uint8_t line_bar[LINE_SIZE] = {0};

uint8_t line_broad_field[LINE_SIZE]; // Green
uint8_t line_short_field[LINE_SIZE]; // Orange
uint8_t line_blank_field[LINE_SIZE]; // White
uint8_t line_broad_short_field[LINE_SIZE]; // Green/Orange
uint8_t line_short_broad_field[LINE_SIZE]; // Orange/Green


#define BIN8(v) \
    ((v & 0x80) ? '1' : '0'), \
    ((v & 0x40) ? '1' : '0'), \
    ((v & 0x20) ? '1' : '0'), \
    ((v & 0x10) ? '1' : '0'), \
    ((v & 0x08) ? '1' : '0'), \
    ((v & 0x04) ? '1' : '0'), \
    ((v & 0x02) ? '1' : '0'), \
    ((v & 0x01) ? '1' : '0')


#define BIN32(v) \
    ((v & 0x80000000U) ? '1' : '0'), \
    ((v & 0x40000000U) ? '1' : '0'), \
    ((v & 0x20000000U) ? '1' : '0'), \
    ((v & 0x10000000U) ? '1' : '0'), \
    ((v & 0x08000000U) ? '1' : '0'), \
    ((v & 0x04000000U) ? '1' : '0'), \
    ((v & 0x02000000U) ? '1' : '0'), \
    ((v & 0x01000000U) ? '1' : '0'), \
    ((v & 0x00800000U) ? '1' : '0'), \
    ((v & 0x00400000U) ? '1' : '0'), \
    ((v & 0x00200000U) ? '1' : '0'), \
    ((v & 0x00100000U) ? '1' : '0'), \
    ((v & 0x00080000U) ? '1' : '0'), \
    ((v & 0x00040000U) ? '1' : '0'), \
    ((v & 0x00020000U) ? '1' : '0'), \
    ((v & 0x00010000U) ? '1' : '0'), \
    ((v & 0x00008000U) ? '1' : '0'), \
    ((v & 0x00004000U) ? '1' : '0'), \
    ((v & 0x00002000U) ? '1' : '0'), \
    ((v & 0x00001000U) ? '1' : '0'), \
    ((v & 0x00000800U) ? '1' : '0'), \
    ((v & 0x00000400U) ? '1' : '0'), \
    ((v & 0x00000200U) ? '1' : '0'), \
    ((v & 0x00000100U) ? '1' : '0'), \
    ((v & 0x00000080U) ? '1' : '0'), \
    ((v & 0x00000040U) ? '1' : '0'), \
    ((v & 0x00000020U) ? '1' : '0'), \
    ((v & 0x00000010U) ? '1' : '0'), \
    ((v & 0x00000008U) ? '1' : '0'), \
    ((v & 0x00000004U) ? '1' : '0'), \
    ((v & 0x00000002U) ? '1' : '0'), \
    ((v & 0x00000001U) ? '1' : '0')

extern void write_font_row(uint8_t *destination, uint8_t character_code, uint8_t attribute_code, uint8_t line_index);
extern void load_glyphs();
extern void copy_glyph_row(uint8_t *destination, uint8_t character_code, uint8_t attribute_code, uint8_t line_index);


extern const uint8_t FONT_WIDTH;
extern const uint8_t FONT_HEIGHT;

void create_broad_field(uint8_t *disaply_line) {
    // Lines: 1,2,314,315
    memset(disaply_line, VOLTS_0_DOUBLE, LINE_DOTS); // Zero the line
    memset(disaply_line + BROAD_SYNC_DOTS_OFFSET, VOLTS_3_DOUBLE, BROAD_SYNC_DOTS); // Lower field
    memset(disaply_line + BROAD_SYNC_DOTS_OFFSET + HALF_LINE_DOTS, VOLTS_3_DOUBLE, BROAD_SYNC_DOTS); // Upper field
}

void create_short_field(uint8_t *disaply_line) {
    // Lines: 4,5,311,312,316,317,624,625
    memset(disaply_line, VOLTS_3_DOUBLE, LINE_DOTS); // 3v the line
    memset(disaply_line + SHORT_SYNC_DOTS_OFFSET, VOLTS_0_DOUBLE, SHORT_SYNC_DOTS); // Lower field
    memset(disaply_line + HALF_LINE_DOTS, VOLTS_0_DOUBLE, SHORT_SYNC_DOTS); // Upper field
}

void create_blank_field(uint8_t *disaply_line) {
    // Lines: 6-22, 319-335
    memset(disaply_line, VOLTS_3_DOUBLE, LINE_DOTS); // 3v the line
    memset(disaply_line + HSYNC_DOTS_OFFSET, VOLTS_0_DOUBLE, HSYNC_DOTS);
    memset(disaply_line + BACK_PORCH_DOTS_OFFSET, VOLTS_3_DOUBLE, BACK_PORCH_DOTS);
    memset(disaply_line + FRONT_PORCH_DOTS_OFFSET, VOLTS_3_DOUBLE, FRONT_PORCH_DOTS);
}


void create_broad_short_field(uint8_t *disaply_line) {
    // Lines: 3
    memset(disaply_line, VOLTS_0_DOUBLE, HALF_LINE_DOTS); // Half 0v
    memset(disaply_line + HALF_LINE_DOTS, VOLTS_3_DOUBLE, HALF_LINE_DOTS); // Half 3v
    memset(disaply_line + BROAD_SYNC_DOTS_OFFSET, VOLTS_3_DOUBLE, BROAD_SYNC_DOTS); // Lower field
    memset(disaply_line + SHORT_SYNC_DOTS_OFFSET + HALF_LINE_DOTS, VOLTS_0_DOUBLE, SHORT_SYNC_DOTS); // Upper field
}

void create_short_broad_field(uint8_t *disaply_line) {
    // Lines: 313
    memset(disaply_line, VOLTS_3_DOUBLE, HALF_LINE_DOTS); // Half 3v
    memset(disaply_line + HALF_LINE_DOTS, VOLTS_0_DOUBLE, HALF_LINE_DOTS); // Half 0v
    memset(disaply_line + SHORT_SYNC_DOTS_OFFSET, VOLTS_0_DOUBLE, SHORT_SYNC_DOTS); // Lower field
    memset(disaply_line + BROAD_SYNC_DOTS_OFFSET + HALF_LINE_DOTS, VOLTS_3_DOUBLE, BROAD_SYNC_DOTS); // Upper field
}

void create_bar_line(uint8_t *disaply_line) {
    int index = SCREEN_DATA_DOTS_OFFSET;
    create_blank_field(disaply_line);

    memset(disaply_line + index + SCREEN_DATA_DOTS / 2, VOLTS_7_DOUBLE, 20);
    index = index + (SCREEN_DATA_DOTS / 2) + 20;
}

void copy_character_line(uint8_t *disaply_line, int disaply_line_count) {
    // Work out row and column
    uint8_t row = disaply_line_count % 11;

    // Blank the line
    memset(disaply_line + HSYNC_DOTS_OFFSET, VOLTS_0_DOUBLE, HSYNC_DOTS);
    memset(disaply_line + BACK_PORCH_DOTS_OFFSET, VOLTS_3_DOUBLE, BACK_PORCH_DOTS);
    memset(disaply_line + FRONT_PORCH_DOTS_OFFSET, VOLTS_3_DOUBLE, FRONT_PORCH_DOTS);

    uint glif_row = screen_row % 11;

    for (int column = 0; column < 80; column++) {
        uint16_t code = screen_data[(screen_row * 80) + column];

        uint8_t ac = (uint8_t)(code & 0xFF);
        uint8_t cc = (uint8_t)(code >> 8);

        int offset = (column * 9) + SCREEN_DATA_DOTS_OFFSET + 45;
        uint8_t *data_area = &disaply_line[offset];
        copy_glyph_row(data_area, cc, ac, row);
    }
    if (row == 0) screen_row += 1;
}

int dma_chan0;
int dma_chan1;
volatile int line_count = 0;

volatile uint8_t active_buffer_index = 0;
uint8_t line_a[LINE_SIZE];
uint8_t line_b[LINE_SIZE];
uint8_t *line_buffers[] = { line_a, line_b };

volatile bool build_pending = false;

uint8_t *active_line; // This is the line that is  being sent out the PIO via DMA
uint8_t *inactive_line; // This is the line that is  being sent out the PIO via DMA


void dma1_irq_handler() {
    // Clear DMA1 interrupt
    dma_hw->ints0 = 1u << dma_chan1;

    // Switch active buffer
    active_buffer_index ^= 1;
    active_line = line_buffers[active_buffer_index];
    inactive_line = line_buffers[active_buffer_index];

    // Start drawing the line
    build_pending = true;

    line_count++;

    if(line_count >= 626) {
        line_count = 1;
        screen_row = 0;
    }
}


int main() {
    set_sys_clock_khz(125000, true);
    stdio_init_all();
    uart_init(UART_ID, BAUD_RATE);

    // Allow time for the USB uart to connect
    sleep_ms(3000);

    create_bar_line(line_bar);
    memset(screen_data, 0, sizeof(screen_data)); // Zero the line


    for (size_t i = 0; i < (sizeof(screen_data) / sizeof(screen_data[0])); i++) {
        screen_data[i] = (i % 10) << 7;
    }

    // screen_data[0 + 10] = 0x0500;
    // screen_data[1 + 10] = 0x6900;
    // screen_data[2 + 10] = 0x6700;
    // screen_data[3 + 10] = 0x6e00;
    // screen_data[4 + 10] = 0x4f00;
    // screen_data[5 + 10] = 0x6e00;

    // 0x5300, 0x6900, 0x6700, 0x6e00, 0x4f00, 0x6e00

    // Setup lines
    create_broad_field(line_broad_field);
    create_short_field(line_short_field);
    create_blank_field(line_blank_field);
    create_broad_short_field(line_broad_short_field);
    create_short_broad_field(line_short_broad_field);

    create_blank_field(line_a);
    create_blank_field(line_b);

    // Use PIO0 for capture of video data
    PIO pio = pio0;

    // -------------------------------
    // Load all programs in PIO memory
    // -------------------------------
    uint offset_dma = pio_add_program(pio, &dma_program);
    uint sm_dma = pio_claim_unused_sm(pio, true);
    dma_program_init(pio, sm_dma, offset_dma, DAC0);

    active_line = line_buffers[active_buffer_index];
    inactive_line = line_buffers[active_buffer_index ^ 1];

    // Set up DMA0 to write to the PIO core
    dma_chan0 = dma_claim_unused_channel(true);
    dma_chan1 = dma_claim_unused_channel(true);

    dma_channel_config c0 = dma_channel_get_default_config(dma_chan0);

    channel_config_set_transfer_data_size(&c0, DMA_SIZE_8);  // One byte at a time
    channel_config_set_read_increment(&c0, true);
    channel_config_set_write_increment(&c0, false);
    channel_config_set_dreq(&c0, pio_get_dreq(pio, sm_dma, true)); // Send byte as pio needs it
    channel_config_set_chain_to(&c0, dma_chan1); // On the end of DMA0 start DMA1

    // Set up DMA0
    dma_channel_configure(
        dma_chan0,
        &c0,
        &pio0_hw->txf[sm_dma], // Write address (the address of the pio's fifo)
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

    printf("Running\n");

    load_glyphs();

    dma_start_channel_mask(1u << dma_chan1) ;

    while(true) {
        if (build_pending) {
            build_pending = false;
            #if 1
            // Work out what to send to the PIO
            switch (line_count) {
                // Broad field
                case 1:
                case 2:
                case 314:
                case 315:
                    memcpy(inactive_line, line_broad_field, LINE_SIZE);
                    break;

                // Mixed broad short field
                case 3:
                    memcpy(inactive_line, line_broad_short_field, LINE_SIZE);
                    break;

                // Mixed short broad field
                case 313:
                    memcpy(inactive_line, line_short_broad_field, LINE_SIZE);
                    break;

                // Short field
                case 4:
                case 5:
                case 311:
                case 312:
                case 316:
                case 317:
                case 624:
                case 625:
                    memcpy(inactive_line, line_short_field, LINE_SIZE);
                    break;

                // Blank screen data
                case 6 ... 34:
                case 319 ... 346:
                case 612 ... 622:
                case 299 ... 310:
                    memcpy(inactive_line, line_blank_field, LINE_SIZE);
                    break;

                // Even screen data
                case 35 ... 298: {
                    int abs_line = ((line_count - 35) * 2) + 1;
                    copy_character_line(inactive_line, abs_line);
                    if (build_pending) printf("OVER RUN\n");
                    break;
                }

                // Odd screen data
                case 347 ... 611: {
                    int abs_line = ((line_count - 347) * 2);
                    copy_character_line(inactive_line, abs_line);
                    if (build_pending) printf("OVER RUN\n");
                    break;
                }

                // Odd half line data (technically ment to have data, but I will blank it)
                case 318:
                    memcpy(inactive_line, line_short_field, HALF_LINE_DOTS);
                    memcpy(inactive_line + HALF_LINE_DOTS, line_blank_field, HALF_LINE_DOTS);
                    break;

                case 623:
                    memcpy(inactive_line, line_blank_field, HALF_LINE_DOTS);
                    memcpy(inactive_line + HALF_LINE_DOTS, line_short_field, HALF_LINE_DOTS);
                    break;

                default:
                    printf("Not in the number range %d", line_count);
                    break;
            }
            #endif
        }
    }
}