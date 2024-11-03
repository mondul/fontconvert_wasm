/*
TrueType to Adafruit_GFX font converter.  Derived from Peter Jakobs'
Adafruit_ftGFX fork & makefont tool, and Paul Kourany's Adafruit_mfGFX.

NOT AN ARDUINO SKETCH.  This is a command-line tool for preprocessing
fonts to be used with the Adafruit_GFX Arduino library.

For web browsers. Outputs to an allocated buffer that MUST be freed on
the client's side.

REQUIRES FREETYPE LIBRARY.  www.freetype.org

Currently this only extracts the printable 7-bit ASCII chars of a font.
Will eventually extend with some int'l chars a la ftGFX, not there yet.
Keep 7-bit fonts around as an option in that case, more compact.

See notes at end for glyph nomenclature & other tidbits.
*/
#ifndef ARDUINO

#include <ctype.h>
#include <ft2build.h>
#include <stdint.h>
#include <stdio.h>
#include FT_GLYPH_H
#include FT_MODULE_H
#include FT_TRUETYPE_DRIVER_H
#include "Adafruit-GFX-Library/gfxfont.h" // Adafruit_GFX font structures

#define DPI 141 // Approximate res. of Adafruit 2.8" TFT

// Output string
char *output;
// Work buffer
char *buf;

// Variables for enbit
uint8_t row, firstCall;

// Accumulate bits for output, with periodic hexadecimal byte write
void enbit(uint8_t value) {
  static uint8_t sum = 0, bit = 0x80;
  if (value)
    sum |= bit;          // Set bit if needed
  if (!(bit >>= 1)) {    // Advance to next bit, end of byte reached?
    if (!firstCall) {    // Format output table nicely
      if (++row >= 12) { // Last entry on line?
        strcat(output, ",\n  "); //   Newline format output
        row = 0;         //   Reset row counter
      } else {           // Not end of line
        strcat(output, ", ");    //   Simple comma delim
      }
    }
    sprintf(buf, "0x%02X", sum); // Write byte value
    strcat(output, buf);
    sum = 0;               // Clear for next byte
    bit = 0x80;            // Reset bit counter
    firstCall = 0;         // Formatting flag
  }
}

/**
 * Exported function for emscripten
 * @param fontName         char*    String pointer with font file name, will be freed here
 * @param fontFileContents uint8_t* Array pointer with font file contents, will be freed here
 * @param fontFileSize     uint16_t Size of the font file
 * @param size             uint8_t  Size of the font characters
 * @param first            uint8_t  First character to process, default is 0x20 (SPACE)
 * @param last             uint8_t  Last character to process, default is 0x7E (~)
 * @return                 char*    String pointer with header file contents, MUST be freed on the JS side
 */
extern char* fontconvert(
  char *fontName,
  uint8_t *fontFileContents,
  uint16_t fontFileSize,
  uint8_t size,
  uint8_t first,
  uint8_t last
) {
  int i, j, err, fontNameLen, bitmapOffset = 0, x, y, byte;
  char c, *ptr;
  FT_Library library;
  FT_Face face;
  FT_Glyph glyph;
  FT_Bitmap *bitmap;
  FT_BitmapGlyphRec *g;
  GFXglyph *table;
  uint8_t bit;

  if (last < first) {
    i = first;
    first = last;
    last = i;
  }

  // Allocate some extra space on fontName
  fontNameLen = strlen(fontName);
  fontName = realloc(fontName, fontNameLen + 20);

  // Derive font table names from filename.  Period (filename
  // extension) is truncated and replaced with the font size & bits.
  ptr = strrchr(fontName, '.'); // Find last period (file ext)
  if (!ptr)
    ptr = &fontName[fontNameLen]; // If none, append
  // Insert font size and 7/8 bit.  fontName was alloc'd w/extra
  // space to allow this, we're not sprintfing into Forbidden Zone.
  sprintf(ptr, "%dpt%db", size, (last > 127) ? 8 : 7);
  // Space and punctuation chars in name replaced w/ underscores.
  for (i = 0; (c = fontName[i]); i++) {
    if (isspace(c) || ispunct(c))
      fontName[i] = '_';
  }

  // Allocate 128 KiB for output. This needs to be freed on the JS side
  output = malloc(131072);

  // Init FreeType lib, load font
  if ((err = FT_Init_FreeType(&library))) {
    sprintf(output, "FreeType init error: %d", err);
    // Free our input pointers
    free(fontName);
    free(fontFileContents);
    return output;
  }

  // Use TrueType engine version 35, without subpixel rendering.
  // This improves clarity of fonts since this library does not
  // support rendering multiple levels of gray in a glyph.
  // See https://github.com/adafruit/Adafruit-GFX-Library/issues/103
  FT_UInt interpreter_version = TT_INTERPRETER_VERSION_35;
  FT_Property_Set(library, "truetype", "interpreter-version",
                  &interpreter_version);

  if ((err = FT_New_Memory_Face(library, fontFileContents, fontFileSize, 0, &face))) {
    sprintf(output, "Font load error: %d", err);
    FT_Done_FreeType(library);
    // Free our input pointers
    free(fontName);
    free(fontFileContents);
    return output;
  }

  // << 6 because '26dot6' fixed-point format
  FT_Set_Char_Size(face, size << 6, 0, DPI, 0);

  // Currently all symbols from 'first' to 'last' are processed.
  // Fonts may contain WAY more glyphs than that, but this code
  // will need to handle encoding stuff to deal with extracting
  // the right symbols, and that's not done yet.
  // fprintf(stderr, "%ld glyphs\n", face->num_glyphs);

  sprintf(output, "const uint8_t %sBitmaps[] PROGMEM = {\n  ", fontName);

  // Allocate space for glyph table
  table = (GFXglyph *)malloc((last - first + 1) * sizeof(GFXglyph));
  // Allocate the work buffer according to the font's name length
  buf = malloc(fontNameLen + 48);

  // Initialize variables for enbit
  row = 0;
  firstCall = 1;

  // Process glyphs and output huge bitmap data array
  for (i = first, j = 0; i <= last; i++, j++) {
    // MONO renderer provides clean image with perfect crop
    // (no wasted pixels) via bitmap struct.
    if ((err = FT_Load_Char(face, i, FT_LOAD_TARGET_MONO))) {
      fprintf(stderr, "Error %d loading char '%c'\n", err, i);
      continue;
    }

    if ((err = FT_Render_Glyph(face->glyph, FT_RENDER_MODE_MONO))) {
      fprintf(stderr, "Error %d rendering char '%c'\n", err, i);
      continue;
    }

    if ((err = FT_Get_Glyph(face->glyph, &glyph))) {
      fprintf(stderr, "Error %d getting glyph '%c'\n", err, i);
      continue;
    }

    bitmap = &face->glyph->bitmap;
    g = (FT_BitmapGlyphRec *)glyph;

    // Minimal font and per-glyph information is stored to
    // reduce flash space requirements.  Glyph bitmaps are
    // fully bit-packed; no per-scanline pad, though end of
    // each character may be padded to next byte boundary
    // when needed.  16-bit offset means 64K max for bitmaps,
    // code currently doesn't check for overflow.  (Doesn't
    // check that size & offsets are within bounds either for
    // that matter...please convert fonts responsibly.)
    table[j].bitmapOffset = bitmapOffset;
    table[j].width = bitmap->width;
    table[j].height = bitmap->rows;
    table[j].xAdvance = face->glyph->advance.x >> 6;
    table[j].xOffset = g->left;
    table[j].yOffset = 1 - g->top;

    for (y = 0; y < bitmap->rows; y++) {
      for (x = 0; x < bitmap->width; x++) {
        byte = x / 8;
        bit = 0x80 >> (x & 7);
        enbit(bitmap->buffer[y * bitmap->pitch + byte] & bit);
      }
    }

    // Pad end of char bitmap to next byte boundary if needed
    int n = (bitmap->width * bitmap->rows) & 7;
    if (n) {     // Pixel count not an even multiple of 8?
      n = 8 - n; // # bits to next multiple
      while (n--)
        enbit(0);
    }
    bitmapOffset += (bitmap->width * bitmap->rows + 7) / 8;

    FT_Done_Glyph(glyph);
  }

  strcat(output, " };\n\n"); // End bitmap array

  // Output glyph attributes table (one per character)
  sprintf(buf, "const GFXglyph %sGlyphs[] PROGMEM = {\n", fontName);
  strcat(output, buf);
  for (i = first, j = 0; i <= last; i++, j++) {
    sprintf(buf, "  { %5d, %3d, %3d, %3d, %4d, %4d }", table[j].bitmapOffset,
           table[j].width, table[j].height, table[j].xAdvance, table[j].xOffset,
           table[j].yOffset);
    strcat(output, buf);
    if (i < last) {
      sprintf(buf, ",   // 0x%02X", i);
      strcat(output, buf);
      if ((i >= ' ') && (i <= '~')) {
        sprintf(buf, " '%c'", i);
        strcat(output, buf);
      }
      strcat(output, "\n");
    }
  }
  sprintf(buf, " }; // 0x%02X", last);
  strcat(output, buf);
  if ((last >= ' ') && (last <= '~')) {
    sprintf(buf, " '%c'", last);
    strcat(output, buf);
  }
  strcat(output, "\n\n");

  // Output font structure
  sprintf(buf, "const GFXfont %s PROGMEM = {\n", fontName);
  strcat(output, buf);
  sprintf(buf, "  (uint8_t  *)%sBitmaps,\n", fontName);
  strcat(output, buf);
  sprintf(buf, "  (GFXglyph *)%sGlyphs,\n", fontName);
  strcat(output, buf);
  if (face->size->metrics.height == 0) {
    // No face height info, assume fixed width and get from a glyph.
    sprintf(buf, "  0x%02X, 0x%02X, %d };\n\n", first, last, table[0].height);
    strcat(output, buf);
  } else {
    sprintf(buf, "  0x%02X, 0x%02X, %ld };\n\n", first, last,
           face->size->metrics.height >> 6);
    strcat(output, buf);
  }
  sprintf(buf, "// Approx. %d bytes\n", bitmapOffset + (last - first + 1) * 7 + 7);
  strcat(output, buf);
  // Size estimate is based on AVR struct and pointer sizes;
  // actual size may vary.

  FT_Done_FreeType(library);

  // Free our allocations
  free(buf);
  free(table);
  // Free our input pointers
  free(fontName);
  free(fontFileContents);
  return output;
}

/* -------------------------------------------------------------------------

Character metrics are slightly different from classic GFX & ftGFX.
In classic GFX: cursor position is the upper-left pixel of each 5x7
character; lower extent of most glyphs (except those w/descenders)
is +6 pixels in Y direction.
W/new GFX fonts: cursor position is on baseline, where baseline is
'inclusive' (containing the bottom-most row of pixels in most symbols,
except those with descenders; ftGFX is one pixel lower).

Cursor Y will be moved automatically when switching between classic
and new fonts.  If you switch fonts, any print() calls will continue
along the same baseline.

                    ...........#####.. -- yOffset
                    ..........######..
                    ..........######..
                    .........#######..
                    ........#########.
   * = Cursor pos.  ........#########.
                    .......##########.
                    ......#####..####.
                    ......#####..####.
       *.#..        .....#####...####.
       .#.#.        ....##############
       #...#        ...###############
       #...#        ...###############
       #####        ..#####......#####
       #...#        .#####.......#####
====== #...# ====== #*###.........#### ======= Baseline
                    || xOffset

glyph->xOffset and yOffset are pixel offsets, in GFX coordinate space
(+Y is down), from the cursor position to the top-left pixel of the
glyph bitmap.  i.e. yOffset is typically negative, xOffset is typically
zero but a few glyphs will have other values (even negative xOffsets
sometimes, totally normal).  glyph->xAdvance is the distance to move
the cursor on the X axis after drawing the corresponding symbol.

There's also some changes with regard to 'background' color and new GFX
fonts (classic fonts unchanged).  See Adafruit_GFX.cpp for explanation.
*/

#endif /* !ARDUINO */
