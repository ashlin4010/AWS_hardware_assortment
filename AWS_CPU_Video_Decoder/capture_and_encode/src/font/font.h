#ifndef FONT_H
#define FONT_H

#define FONT_WIDTH 9
#define FONT_HEIGHT 11

void copy_glyph_row(uint8_t *destination, uint8_t character_code, uint8_t attribute_code, uint8_t line_index);

void load_glyphs();

#endif /* FONT_H */