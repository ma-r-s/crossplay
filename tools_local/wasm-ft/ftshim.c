/* The eleven FreeType calls mono_cpfont.py makes, exported to wasm.
 *
 * This is not a FreeType binding; it is the exact surface the study font
 * pipeline touches, and nothing else. The Python side (web_shims/freetype.py)
 * mirrors freetype-py's names over these, so mono_cpfont.py cannot tell which
 * implementation it is talking to. Byte-identical output against the CLI is
 * the acceptance test, which is why this compiles the same FreeType release
 * (2.13.2) with default options, like the freetype-py wheel does.
 */

#include <ft2build.h>
#include <stdio.h>
#include FT_FREETYPE_H
#include <emscripten/emscripten.h>

static FT_Library library;

EMSCRIPTEN_KEEPALIVE int ft_init(void) {
  if (library) return 0;
  return FT_Init_FreeType(&library);
}

EMSCRIPTEN_KEEPALIVE const char* ft_version(void) {
  static char buf[32];
  FT_Int major, minor, patch;
  FT_Library_Version(library, &major, &minor, &patch);
  snprintf(buf, sizeof buf, "%d.%d.%d", major, minor, patch);
  return buf;
}

/* The font bytes must stay alive for the face's lifetime; the caller keeps
 * the malloc'd buffer and frees it after ft_done_face. */
EMSCRIPTEN_KEEPALIVE FT_Face ft_new_memory_face(const unsigned char* data, long size, long index) {
  FT_Face face = NULL;
  if (FT_New_Memory_Face(library, data, size, index, &face)) return NULL;
  return face;
}

EMSCRIPTEN_KEEPALIVE void ft_done_face(FT_Face face) { FT_Done_Face(face); }

EMSCRIPTEN_KEEPALIVE int ft_set_char_size(FT_Face face, long width, long height, unsigned hres, unsigned vres) {
  return FT_Set_Char_Size(face, width, height, hres, vres);
}

EMSCRIPTEN_KEEPALIVE unsigned ft_get_char_index(FT_Face face, unsigned long code) {
  return FT_Get_Char_Index(face, code);
}

EMSCRIPTEN_KEEPALIVE int ft_load_glyph(FT_Face face, unsigned index, int flags) {
  return FT_Load_Glyph(face, index, flags);
}

/* Glyph slot accessors, valid after a successful ft_load_glyph. */
EMSCRIPTEN_KEEPALIVE const unsigned char* ft_bitmap_buffer(FT_Face face) { return face->glyph->bitmap.buffer; }
EMSCRIPTEN_KEEPALIVE int ft_bitmap_pitch(FT_Face face) { return face->glyph->bitmap.pitch; }
EMSCRIPTEN_KEEPALIVE unsigned ft_bitmap_width(FT_Face face) { return face->glyph->bitmap.width; }
EMSCRIPTEN_KEEPALIVE unsigned ft_bitmap_rows(FT_Face face) { return face->glyph->bitmap.rows; }
EMSCRIPTEN_KEEPALIVE int ft_bitmap_left(FT_Face face) { return face->glyph->bitmap_left; }
EMSCRIPTEN_KEEPALIVE int ft_bitmap_top(FT_Face face) { return face->glyph->bitmap_top; }
EMSCRIPTEN_KEEPALIVE long ft_linear_hori_advance(FT_Face face) { return face->glyph->linearHoriAdvance; }

/* Size metrics, valid after ft_set_char_size. 26.6 fixed point, like the
 * SizeMetrics freetype-py exposes. */
EMSCRIPTEN_KEEPALIVE long ft_size_height(FT_Face face) { return face->size->metrics.height; }
EMSCRIPTEN_KEEPALIVE long ft_size_ascender(FT_Face face) { return face->size->metrics.ascender; }
EMSCRIPTEN_KEEPALIVE long ft_size_descender(FT_Face face) { return face->size->metrics.descender; }
