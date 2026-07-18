#ifndef VIDEO_H
#define VIDEO_H

#include "font/font.h"
#include <stdbool.h>

#define SCREEN_WIDTH 80
#define SCREEN_HEIGHT 48

#define VOLTS_0 0x00
#define VOLTS_2 0x04
#define VOLTS_3 0x02
#define VOLTS_4 0x01
#define VOLTS_5 0x03
#define VOLTS_6 0x05
#define VOLTS_7 0x06
#define VOLTS_M 0x07

#define VOLTS_0_DOUBLE 0x00
#define VOLTS_2_DOUBLE 0x24
#define VOLTS_3_DOUBLE 0x12
#define VOLTS_4_DOUBLE 0x09
#define VOLTS_5_DOUBLE 0x1B
#define VOLTS_6_DOUBLE 0x2D
#define VOLTS_7_DOUBLE 0x36
#define VOLTS_M_DOUBLE 0x3F

#define ZERO_BRIGHT VOLTS_3
#define FULL_BRIGHT VOLTS_M
#define HALF_BRIGHT VOLTS_6

#define ZERO_BRIGHT_D VOLTS_3_DOUBLE
#define FULL_BRIGHT_D VOLTS_M_DOUBLE
#define HALF_BRIGHT_D VOLTS_6_DOUBLE

#define LINE_SIZE 1000
#define HALF_LINE_SIZE 500

extern uint8_t line_broad_field[LINE_SIZE]; // Green
extern uint8_t line_short_field[LINE_SIZE]; // Orange
extern uint8_t line_blank_field[LINE_SIZE]; // White
extern uint8_t line_broad_short_field[LINE_SIZE]; // Green/Orange
extern uint8_t line_short_broad_field[LINE_SIZE]; // Orange/Green

extern bool is_high_resolution;

void video_init();

void copy_character_line(uint8_t *disaply_line, uint32_t screen_row, uint32_t glyph_row);

void fill_screen_with_character(uint8_t character_code, uint8_t attribute_code);

void screen_set_character(uint8_t character_code, uint8_t attribute_code, uint8_t row, uint8_t column);

void copy_buffer_to_screen(const uint16_t *buffer, size_t size, uint8_t start_row);

static inline void write_scanline(uint8_t *inactive_line) {
    static int line_count;
    bool double_resolution = false;

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
            uint32_t feild_line = is_high_resolution ? ((line_count - 35) * 2) + 1 : line_count - 35;
            uint32_t font_row = feild_line % FONT_HEIGHT;
            uint32_t screen_row = feild_line / FONT_HEIGHT;
            copy_character_line(inactive_line, screen_row, font_row);
            break;
        }

        // Odd screen data
        case 347 ... 611: {
            uint32_t feild_line = is_high_resolution ? (line_count - 347) * 2 : line_count - 347;
            uint32_t font_row = feild_line % FONT_HEIGHT;
            uint32_t screen_row = feild_line / FONT_HEIGHT;
            copy_character_line(inactive_line, screen_row, font_row);
            break;
        }

        // Odd half line data (technically ment to have data, but I will blank it)
        case 318:
            memcpy(inactive_line, line_short_field, HALF_LINE_SIZE);
            memcpy(inactive_line + HALF_LINE_SIZE, line_blank_field, HALF_LINE_SIZE);
        break;

        case 623:
            memcpy(inactive_line, line_blank_field, HALF_LINE_SIZE);
            memcpy(inactive_line + HALF_LINE_SIZE, line_short_field, HALF_LINE_SIZE);
        break;

        default:
            printf("Not in the number range %d", line_count);
        break;
    }

    if(++line_count >= 626) {
        line_count = 1;
    }
}

#endif /* VIDEO_H */