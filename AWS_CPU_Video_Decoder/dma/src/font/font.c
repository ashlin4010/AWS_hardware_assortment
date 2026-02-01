#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "../src/video.h"
#include "font.h"
#include "font_data.h"

#define PRINT_FONT 0 // Enable to log glyphs to console for debugging
#define GLYPH_CHARACTER_COUNT 256 // Number of unique characters in the font
#define GLYPH_VARIANTS 8 // Number of unique glyphs for each character (Normal, Reverse, Underline, etc)
#define ATTRIBUTE_COUNT 16 // Number of possable attributes, 2^4 = 16

// Font bitmaps
static uint8_t glyph[(GLYPH_CHARACTER_COUNT * GLYPH_VARIANTS)][FONT_WIDTH * FONT_HEIGHT]; // 256*8 buffers of [FONT_WIDTH * FONT_HEIGHT]
static uint8_t *glyph_table[GLYPH_CHARACTER_COUNT][ATTRIBUTE_COUNT]; // 256 arrays of pointers to glyphs
static uint8_t *default_glyphs[ATTRIBUTE_COUNT] = {
    NULL, // Normal
    NULL, // Normal
    blank_glyph,
    blank_glyph,
    NULL, // Normal Reverse
    NULL, // Normal Reverse Underline
    blank_reverse_glyph,
    blank_reverse_glyph,
    NULL, // Half Normal
    NULL,
    blank_glyph,
    blank_glyph,
    NULL, // Half Normal Reverse
    NULL, // Half Normal Reverse Underline
    half_blank_reverse_glyph,
    half_blank_reverse_glyph
};


void copy_glyph_row(uint8_t *destination, uint8_t character_code, uint8_t attribute_code, uint8_t line_index) {
    uint8_t *glyph = glyph_table[character_code][attribute_code];
    uint8_t *src = glyph + line_index * FONT_WIDTH;

    destination[0] = src[0];
    destination[1] = src[1];
    destination[2] = src[2];
    destination[3] = src[3];
    destination[4] = src[4];
    destination[5] = src[5];
    destination[6] = src[6];
    destination[7] = src[7];
    destination[8] = src[8];
}

static void build_glyph(uint8_t *destination, uint8_t character_code, uint8_t attribute_code) {
    uint8_t alternate_font = character_code >> 7;
    uint8_t line_drawing_mode = (!!((character_code - 128) >= 64)) && alternate_font;

    uint8_t reverse_video = (attribute_code >> 2) & 1;
    uint8_t half_video = (attribute_code >> 3) & 1;
    uint8_t underline_video = (attribute_code >> 0) & 1;

    for(uint8_t line_index = 0; line_index < FONT_HEIGHT; line_index++) {
        uint16_t font_offset = 0x00;
        uint8_t half_bit_shift = 0;

        // Calculate font rom address
        font_offset |= (character_code & 0x7F) | ((line_index & 0x0F) << 7);

        // Is this an alternate font, load alternate font
        if (alternate_font) font_offset |= (1u << 11);

        // Do we need to half bit shift this font row?
        half_bit_shift = !(FONT[font_offset] & 0x80);

        uint32_t glyph_row = 0; // Build the complete glyph row here
        uint32_t shift = 2;     // Each pixel is made of two dots, glyphs are paded by one pixel eg two dots

        if (line_drawing_mode) {
            uint8_t f0 = (FONT[font_offset] >> 0) & 1;
            uint8_t f1 = (FONT[font_offset] >> 1) & 1;
            uint8_t f2 = (FONT[font_offset] >> 2) & 1;
            uint8_t f3 = (FONT[font_offset] >> 3) & 1;
            uint8_t f4 = (FONT[font_offset] >> 4) & 1;
            uint8_t f5 = (FONT[font_offset] >> 5) & 1;
            uint8_t f6 = (FONT[font_offset] >> 6) & 1;

            glyph_row |= (f0 << 0) | (f0 << 0 + 1); // Row 0
            glyph_row |= (f2 << (1 * shift)) | (f2 << (1 * shift) + 1); // Row 1
            glyph_row |= (f2 << (2 * shift)) | (f2 << (2 * shift) + 1); // Row 2
            glyph_row |= (f2 << (3 * shift)) | (f2 << (3 * shift) + 1); // Row 3
            glyph_row |= (f3 << (4 * shift)) | (f3 << (4 * shift) + 1); // Row 4
            glyph_row |= (f4 << (5 * shift)) | (f4 << (5 * shift) + 1); // Row 5
            glyph_row |= (f5 << (6 * shift)) | (f5 << (6 * shift) + 1); // Row 6
            glyph_row |= (f6 << (7 * shift)) | (f6 << (7 * shift) + 1); // Row 7
            glyph_row |= (f1 << (8 * shift)) | (f1 << (8 * shift) + 1); // Row 8
        } else {
            // Build bit map proto glyph
            for (int i = 0; i < 7; i++) {
                uint32_t bit = (FONT[font_offset] >> i) & 1;
                glyph_row |= (bit * 3u) << shift;
                shift += 2;
            }

            // Add single dot shift
            if (half_bit_shift) {
                glyph_row <<= 1;
            }

            // Set underline
            if(underline_video && line_index == 9) {
                glyph_row = 0x3FFFF;
            }
        }

        // Work out DAC values (reverse, half, full)
        uint8_t active = reverse_video ? ZERO_BRIGHT : (half_video ? HALF_BRIGHT : FULL_BRIGHT);
        uint8_t inactive = reverse_video ? (half_video ? HALF_BRIGHT : FULL_BRIGHT) : ZERO_BRIGHT;

        // Replace dots with DAC values
        for (int i = 0; i < FONT_WIDTH; i++) {
            uint8_t f = (glyph_row >> i * 2) & 1 ? active : inactive; // dot 1
            uint8_t s = (glyph_row >> (i * 2) + 1) & 1 ? active : inactive; // dot 2
            uint8_t y = f | (s << 3); // double pack

            #if PRINT_FONT
            char debug_dot_1;
            char debug_dot_2;
            if(f == FULL_BRIGHT) debug_dot_1 = 'X';
            if(f == HALF_BRIGHT) debug_dot_1 = '.';
            if(f == ZERO_BRIGHT) debug_dot_1 = ' ';

            if(s == FULL_BRIGHT) debug_dot_2 = 'X';
            if(s == HALF_BRIGHT) debug_dot_2 = '.';
            if(s == ZERO_BRIGHT) debug_dot_2 = ' ';

            printf("%c%c", debug_dot_1, debug_dot_2);
            #endif

            destination[i] = y; // Write the byte (two dots) to the destination, move to the next pixel
        }

        #if PRINT_FONT
        printf("\n");
        #endif
        destination += FONT_WIDTH;
    }
    #if PRINT_FONT
    printf("\n");
    #endif
}

void load_glyphs() {
    for(int cc = 0; cc < GLYPH_CHARACTER_COUNT; cc++) {
        uint8_t *glyph_storage;

        memcpy(glyph_table[cc], default_glyphs, sizeof(glyph_table[cc]));

        // Normal
        glyph_storage = glyph[(cc * GLYPH_VARIANTS)];
        build_glyph(glyph_storage, cc, 0x00);
        glyph_table[cc][0x00] = glyph_storage;

        //Underline 0x01
        glyph_storage = glyph[(cc * GLYPH_VARIANTS) + 1];
        build_glyph(glyph_storage, cc, 0x01);
        glyph_table[cc][0x01] = glyph_storage;

        // Normal Reverse
        glyph_storage = glyph[(cc * GLYPH_VARIANTS) + 2];
        build_glyph(glyph_storage, cc, 0x04);
        glyph_table[cc][0x04] = glyph_storage;

        // Underline Reverse 0x05
        glyph_storage = glyph[(cc * GLYPH_VARIANTS) + 3];
        build_glyph(glyph_storage, cc, 0x05);
        glyph_table[cc][0x05] = glyph_storage;

        // Half Normal
        glyph_storage = glyph[(cc * GLYPH_VARIANTS) + 4];
        build_glyph(glyph_storage, cc, 0x08);
        glyph_table[cc][0x08] = glyph_storage;

        // Half Underline  0x09
        glyph_storage = glyph[(cc * GLYPH_VARIANTS) + 5];
        build_glyph(glyph_storage, cc, 0x09);
        glyph_table[cc][0x09] = glyph_storage;

        // Half Normal Reverse
        glyph_storage = glyph[(cc * GLYPH_VARIANTS) + 6];
        build_glyph(glyph_storage, cc, 0x0C);
        glyph_table[cc][0x0C] = glyph_storage;

        // Half Underline Reverse 0x0D
        glyph_storage = glyph[(cc * GLYPH_VARIANTS) + 7];
        build_glyph(glyph_storage, cc, 0x0D);
        glyph_table[cc][0x0D] = glyph_storage;
    }
}