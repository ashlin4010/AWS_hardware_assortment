#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "font/font.h"
#include "video.h"

// #define SCREEN_WIDTH 80
// #define SCREEN_HEIGHT 48

const uint32_t HSYNC_DOTS = 74;
const uint32_t BACK_PORCH_DOTS = 88;
const uint32_t SCREEN_DATA_DOTS = 812;
const uint32_t FRONT_PORCH_DOTS = 26;
const uint32_t SHORT_SYNC_DOTS = 20;
const uint32_t BROAD_SYNC_DOTS = 74;

const uint32_t HSYNC_DOTS_OFFSET = 0;
const uint32_t BACK_PORCH_DOTS_OFFSET = HSYNC_DOTS_OFFSET + HSYNC_DOTS;
const uint32_t SCREEN_DATA_DOTS_OFFSET = BACK_PORCH_DOTS_OFFSET + BACK_PORCH_DOTS;
const uint32_t FRONT_PORCH_DOTS_OFFSET = SCREEN_DATA_DOTS_OFFSET + SCREEN_DATA_DOTS;
const uint32_t SHORT_SYNC_DOTS_OFFSET = 0;
const uint32_t BROAD_SYNC_DOTS_OFFSET = HALF_LINE_SIZE - BROAD_SYNC_DOTS;

uint8_t line_broad_field[LINE_SIZE]; // Green
uint8_t line_short_field[LINE_SIZE]; // Orange
uint8_t line_blank_field[LINE_SIZE]; // White
uint8_t line_broad_short_field[LINE_SIZE]; // Green/Orange
uint8_t line_short_broad_field[LINE_SIZE]; // Orange/Green

uint16_t screen_data[SCREEN_HEIGHT * SCREEN_WIDTH];

static void create_broad_field(uint8_t *disaply_line) {
    // Lines: 1,2,314,315
    memset(disaply_line, VOLTS_0_DOUBLE, LINE_SIZE); // Zero the line
    memset(disaply_line + BROAD_SYNC_DOTS_OFFSET, VOLTS_3_DOUBLE, BROAD_SYNC_DOTS); // Lower field
    memset(disaply_line + BROAD_SYNC_DOTS_OFFSET + HALF_LINE_SIZE, VOLTS_3_DOUBLE, BROAD_SYNC_DOTS); // Upper field
}

static void create_short_field(uint8_t *disaply_line) {
    // Lines: 4,5,311,312,316,317,624,625
    memset(disaply_line, VOLTS_3_DOUBLE, LINE_SIZE); // 3v the line
    memset(disaply_line + SHORT_SYNC_DOTS_OFFSET, VOLTS_0_DOUBLE, SHORT_SYNC_DOTS); // Lower field
    memset(disaply_line + HALF_LINE_SIZE, VOLTS_0_DOUBLE, SHORT_SYNC_DOTS); // Upper field
}

static void create_blank_field(uint8_t *disaply_line) {
    // Lines: 6-22, 319-335
    memset(disaply_line, VOLTS_3_DOUBLE, LINE_SIZE); // 3v the line
    memset(disaply_line + HSYNC_DOTS_OFFSET, VOLTS_0_DOUBLE, HSYNC_DOTS);
    memset(disaply_line + BACK_PORCH_DOTS_OFFSET, VOLTS_3_DOUBLE, BACK_PORCH_DOTS);
    memset(disaply_line + FRONT_PORCH_DOTS_OFFSET, VOLTS_3_DOUBLE, FRONT_PORCH_DOTS);
}


static void create_broad_short_field(uint8_t *disaply_line) {
    // Lines: 3
    memset(disaply_line, VOLTS_0_DOUBLE, HALF_LINE_SIZE); // Half 0v
    memset(disaply_line + HALF_LINE_SIZE, VOLTS_3_DOUBLE, HALF_LINE_SIZE); // Half 3v
    memset(disaply_line + BROAD_SYNC_DOTS_OFFSET, VOLTS_3_DOUBLE, BROAD_SYNC_DOTS); // Lower field
    memset(disaply_line + SHORT_SYNC_DOTS_OFFSET + HALF_LINE_SIZE, VOLTS_0_DOUBLE, SHORT_SYNC_DOTS); // Upper field
}

static void create_short_broad_field(uint8_t *disaply_line) {
    // Lines: 313
    memset(disaply_line, VOLTS_3_DOUBLE, HALF_LINE_SIZE); // Half 3v
    memset(disaply_line + HALF_LINE_SIZE, VOLTS_0_DOUBLE, HALF_LINE_SIZE); // Half 0v
    memset(disaply_line + SHORT_SYNC_DOTS_OFFSET, VOLTS_0_DOUBLE, SHORT_SYNC_DOTS); // Lower field
    memset(disaply_line + BROAD_SYNC_DOTS_OFFSET + HALF_LINE_SIZE, VOLTS_3_DOUBLE, BROAD_SYNC_DOTS); // Upper field
}

void copy_character_line(uint8_t *disaply_line, uint32_t screen_row, uint32_t glyph_row) {
    uint32_t column = 0;

    // Blank the line
    memset(disaply_line + HSYNC_DOTS_OFFSET, VOLTS_0_DOUBLE, HSYNC_DOTS);
    memset(disaply_line + BACK_PORCH_DOTS_OFFSET, VOLTS_3_DOUBLE, BACK_PORCH_DOTS);
    memset(disaply_line + FRONT_PORCH_DOTS_OFFSET, VOLTS_3_DOUBLE, FRONT_PORCH_DOTS);

    // Do not draw lines past the maximum number of screen rows
    if(screen_row >= SCREEN_HEIGHT) {
        memset(disaply_line + SCREEN_DATA_DOTS_OFFSET, VOLTS_3_DOUBLE, SCREEN_DATA_DOTS);
        return;
    }

    // Write each column on the line
    for (int column = 0; column < SCREEN_WIDTH; column++) {
        uint16_t code = screen_data[(screen_row * SCREEN_WIDTH) + column];

        uint8_t ac = 0x0F & (uint8_t)(code >> 8);
        uint8_t cc = (uint8_t)(code & 0xFF);

        int offset = (column * 9) + SCREEN_DATA_DOTS_OFFSET + 45;
        uint8_t *data_area = &disaply_line[offset];
        copy_glyph_row(data_area, cc, ac, glyph_row);
    }
}

void fill_screen_with_character(uint8_t character_code, uint8_t attribute_code) {
    uint16_t character_data = ((uint16_t)character_code << 8) | attribute_code;

    for(int i = 0; i < sizeof(screen_data) / sizeof(screen_data[0]); i++ ) {
        screen_data[i] = character_data;
    }
}

void screen_set_character(uint8_t character_code, uint8_t attribute_code, uint8_t row, uint8_t column) {
    uint16_t character_data = ((uint16_t)character_code << 8) | attribute_code;
    screen_data[(row * SCREEN_WIDTH) + column] = character_data;
}


void copy_buffer_to_screen(const uint16_t *buffer, size_t size, uint8_t start_row) {
    if (start_row >= SCREEN_HEIGHT || size == 0 || buffer == NULL) {
        return; // nothing to do
    }

    // Calculate the maximum number of elements we can safely copy
    size_t max_elements = (SCREEN_HEIGHT - start_row) * SCREEN_WIDTH;
    size_t elements_to_copy = size < max_elements ? size : max_elements;

    // Copy the data
    memcpy(&screen_data[start_row * SCREEN_WIDTH], buffer, elements_to_copy * sizeof(uint16_t));
}

void video_init() {
    load_glyphs();

    // Setup lines
    create_broad_field(line_broad_field);
    create_short_field(line_short_field);
    create_blank_field(line_blank_field);
    create_broad_short_field(line_broad_short_field);
    create_short_broad_field(line_short_broad_field);
}