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
#define VOLTS_0 0x00
#define VOLTS_2 0x24
#define VOLTS_3 0x12
#define VOLTS_4 0x09
#define VOLTS_5 0x1B
#define VOLTS_6 0x2D
#define VOLTS_7 0x36
#define VOLTS_M 0x3F

#define LINE_SIZE 1000

static uint32_t dma_dummy;

const uint LINE_DOTS = 1000;
const uint HALF_LINE_DOTS = LINE_DOTS / 2;

const uint HSYNC_DOTS = 74;
const uint BACK_PORCH_DOTS = 88;
const uint SCREEN_DATA_DOTS = 812;
const uint FRONT_PORCH_DOTS = 26;

const uint SHORT_SYNC_DOTS = 37;
const uint BROAD_SYNC_DOTS = 74;
const uint HSYNC_DOTS_OFFSET = 0;

const uint BACK_PORCH_DOTS_OFFSET = HSYNC_DOTS_OFFSET + HSYNC_DOTS;
const uint SCREEN_DATA_DOTS_OFFSET =  BACK_PORCH_DOTS_OFFSET + BACK_PORCH_DOTS;
const uint FRONT_PORCH_DOTS_OFFSET = SCREEN_DATA_DOTS_OFFSET + SCREEN_DATA_DOTS;
const uint SHORT_SYNC_DOTS_OFFSET = 0;
const uint BROAD_SYNC_DOTS_OFFSET = (LINE_DOTS / 2) - BROAD_SYNC_DOTS;

const uint screen_text_rows = 47;
const uint screen_text_columns = 80;

uint8_t line_bar[LINE_SIZE];
uint8_t line_vsync[LINE_SIZE] = {0};

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

extern const uint8_t FONT_WIDTH;
extern const uint8_t FONT_HEIGHT;

void create_broad_field(uint8_t *disaply_line) {
    // Lines: 1,2,314,315
    memset(disaply_line, VOLTS_0, LINE_DOTS); // Zero the line
    memset(disaply_line + BROAD_SYNC_DOTS_OFFSET, VOLTS_3, BROAD_SYNC_DOTS); // Lower field
    memset(disaply_line + BROAD_SYNC_DOTS_OFFSET + HALF_LINE_DOTS, VOLTS_3, BROAD_SYNC_DOTS); // Upper field
}

void create_short_field(uint8_t *disaply_line) {
    // Lines: 4,5,311,312,316,317,624,625
    memset(disaply_line, VOLTS_3, LINE_DOTS); // 3v the line
    memset(disaply_line + SHORT_SYNC_DOTS_OFFSET, VOLTS_0, SHORT_SYNC_DOTS); // Lower field
    memset(disaply_line + HALF_LINE_DOTS, VOLTS_0, SHORT_SYNC_DOTS); // Upper field
}

void create_blank_field(uint8_t *disaply_line) {
    // Lines: 6-22, 319-335
    memset(disaply_line, VOLTS_3, LINE_DOTS); // 3v the line
    memset(disaply_line + HSYNC_DOTS_OFFSET, VOLTS_0, HSYNC_DOTS);
    memset(disaply_line + BACK_PORCH_DOTS_OFFSET, VOLTS_3, BACK_PORCH_DOTS);
    memset(disaply_line + FRONT_PORCH_DOTS_OFFSET, VOLTS_3, FRONT_PORCH_DOTS);
}


void create_broad_short_field(uint8_t *disaply_line) {
    // Lines: 3
    memset(disaply_line, VOLTS_0, HALF_LINE_DOTS); // Half 0v
    memset(disaply_line + HALF_LINE_DOTS, VOLTS_3, HALF_LINE_DOTS); // Half 3v
    memset(disaply_line + BROAD_SYNC_DOTS_OFFSET, VOLTS_3, BROAD_SYNC_DOTS); // Lower field
    memset(disaply_line + SHORT_SYNC_DOTS_OFFSET + HALF_LINE_DOTS, VOLTS_0, SHORT_SYNC_DOTS); // Upper field
}

void create_short_broad_field(uint8_t *disaply_line) {
    // Lines: 313
    memset(disaply_line, VOLTS_3, HALF_LINE_DOTS); // Half 3v
    memset(disaply_line + HALF_LINE_DOTS, VOLTS_0, HALF_LINE_DOTS); // Half 0v
    memset(disaply_line + SHORT_SYNC_DOTS_OFFSET, VOLTS_0, SHORT_SYNC_DOTS); // Lower field
    memset(disaply_line + BROAD_SYNC_DOTS_OFFSET + HALF_LINE_DOTS, VOLTS_3, BROAD_SYNC_DOTS); // Upper field
}

void create_bar_line(uint8_t *disaply_line) {
    int index = SCREEN_DATA_DOTS_OFFSET;
    create_blank_field(disaply_line);
    memset(disaply_line + index, VOLTS_3, 62);
    index = index + 62;
    memset(disaply_line + index, VOLTS_M, 90);
    index = index + 90;
    memset(disaply_line + index, VOLTS_4, 135);
    index = index + 135;
    memset(disaply_line + index, VOLTS_5, 135);
    index = index + 135;
    memset(disaply_line + index, VOLTS_6, 135);
    index = index + 135;
    memset(disaply_line + index, VOLTS_7, 135);
    index = index + 135;
    memset(disaply_line + index, VOLTS_M, 135);
    index = index + 135;
    memset(disaply_line + index, VOLTS_3, 30);
    index = index + 30;
}

void copy_character_line(uint8_t *disaply_line, int disaply_line_count) {
    // Work out row and column
    int row = disaply_line_count % 11;

    memset(disaply_line + HSYNC_DOTS_OFFSET, VOLTS_0, HSYNC_DOTS);
    memset(disaply_line + BACK_PORCH_DOTS_OFFSET, VOLTS_3, BACK_PORCH_DOTS);
    memset(disaply_line + FRONT_PORCH_DOTS_OFFSET, VOLTS_3, FRONT_PORCH_DOTS);

    for (int column = 0; column < 2; column++) {
        int offset = (column * 9) + SCREEN_DATA_DOTS_OFFSET + 45;
        uint8_t *data_area = &disaply_line[offset];

        write_font_row(data_area, (disaply_line_count / 11) + 1, 0, (uint8_t)row);
    }
}

int dma_chan0;
int dma_chan1;
int line_count = 0;

uint8_t active_buffer_index = 0;
uint8_t line_a[LINE_SIZE];
uint8_t line_b[LINE_SIZE];
uint8_t *line_buffers[] = { line_a, line_b };

// uint8_t *active_line = line_buffers[active_buffer_index];
// uint8_t *next_line = line_buffers[active_buffer_index ^ 1];


void dma_handler() {
    dma_hw->ints0 = 1u << dma_chan0;

    // Switch active buffer
    active_buffer_index ^= 1;

    // Reload DMA0
    dma_channel_set_read_addr(
        dma_chan0,
        line_buffers[active_buffer_index],
        true
    );

    // Reload DMA1
    dma_channel_set_read_addr(
        dma_chan1,
        &dma_dummy,
        true
    );
}


void dma1_irq_handler() {
    // Clear DMA1 interrupt
    dma_hw->ints0 = 1u << dma_chan1;
    line_count++;

    if(line_count >= 626) {
        line_count = 1;
    }

    #if 1
    // Work out what to send to the PIO
    switch (line_count) {
        // Broad field
        case 1:
        case 2:
        case 314:
        case 315:
            memcpy(line_buffers[active_buffer_index ^ 1], line_broad_field, LINE_SIZE);
            break;

        // Mixed broad short field
        case 3:
            memcpy(line_buffers[active_buffer_index ^ 1], line_broad_short_field, LINE_SIZE);
            break;

        // Mixed short broad field
        case 313:
            memcpy(line_buffers[active_buffer_index ^ 1], line_short_broad_field, LINE_SIZE);
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
            memcpy(line_buffers[active_buffer_index ^ 1], line_short_field, LINE_SIZE);
            break;

        // Blank screen data
        case 6 ... 34:
        case 319 ... 346:
        case 612 ... 622:
        case 300 ... 310:
            memcpy(line_buffers[active_buffer_index ^ 1], line_blank_field, LINE_SIZE);
            break;

        // Even screen data
        case 35 ... 299: {
            uint8_t *nonactive_line = line_buffers[active_buffer_index ^ 1];
            int abs_line = (line_count - 35) * 2;

            copy_character_line(nonactive_line, abs_line);
            break;
        }

        // Odd screen data
        case 347 ... 611: {
            uint8_t *nonactive_line = line_buffers[active_buffer_index ^ 1];
            int abs_line = ((line_count - 347) * 2 ) - 1;

            copy_character_line(nonactive_line, abs_line);
            break;
        }

        // Odd half line data (technically ment to have data, but I will blank it)
        case 318:
            memcpy(line_buffers[active_buffer_index ^ 1], line_short_field, 500);
            memcpy(line_buffers[active_buffer_index ^ 1] + 500, line_blank_field, 500);
            break;

        case 623:
            memcpy(line_buffers[active_buffer_index ^ 1], line_blank_field, 500);
            memcpy(line_buffers[active_buffer_index ^ 1] + 500, line_short_field, 500);
            break;

        default:
            printf("Not in the number range %d", line_count);
            break;
    }
    #endif
}


int main() {
    set_sys_clock_khz(125000, true);
    stdio_init_all();
    uart_init(UART_ID, BAUD_RATE);

    // Allow time for the USB uart to connect
    sleep_ms(3000);

    create_bar_line(line_bar);

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

    // Set up DMA0 to write to the PIO core
    dma_chan0 = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config(dma_chan0);

    channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_dreq(&c, pio_get_dreq(pio, sm_dma, true));

    dma_channel_configure(
        dma_chan0,
        &c,
        &pio0_hw->txf[sm_dma], // Write address (only need to set this once)
        line_blank_field,      // Don't provide a read address yet
        1000,                 // Write the same value many times, then halt and interrupt
        true                  // Don't start yet
    );

    // Tell the DMA to raise IRQ line 0 when the channel finishes a block
    dma_channel_set_irq0_enabled(dma_chan0, true);

    // Configure the processor to run dma_handler() when DMA IRQ 0 is asserted
    irq_set_exclusive_handler(DMA_IRQ_0, dma_handler);
    irq_set_enabled(DMA_IRQ_0, true);


    // DMA1 is being used as a timer. It moves half as many bytes as DMA0
    // When it stops we trigger an ISR and get the next line readdy
    // When DMA0 stop it swaps line buffers and retsart DMA1
    dma_chan1 = dma_claim_unused_channel(true);
    dma_channel_config c1 = dma_channel_get_default_config(dma_chan1);
    channel_config_set_transfer_data_size(&c1, DMA_SIZE_8);
    channel_config_set_read_increment(&c1, false);
    channel_config_set_write_increment(&c1, false);
    channel_config_set_dreq(&c1, pio_get_dreq(pio, sm_dma, true));

    dma_channel_configure(
        dma_chan1,
        &c1,
        &dma_dummy,
        &dma_dummy,
        700, // trigger at halfway point
        true
    );

    dma_channel_set_irq1_enabled(dma_chan1, true);
    irq_set_exclusive_handler(DMA_IRQ_1, dma1_irq_handler);
    irq_set_enabled(DMA_IRQ_1, true);

    printf("Running\n");

    while(true) {
        tight_loop_contents();
    }

}