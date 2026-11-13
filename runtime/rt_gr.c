/* Text-cell and glyph routines that TempleOS wrote in assembly (Adam/Gr/GrTextBase.HC, GrAsm.HC). */
#include <string.h>
#include "rt.h"

#define TEXT_COLS 80
#define TEXT_ROWS 60
#define ATTRf_UNDERLINE 31

/* Clip a run of `len` cells at text (x,y) in task's window, as _TEXT_LEN_STR does.
   Returns the first cell or NULL; *len and *skip (source chars clipped on the left) updated. */
static uint32_t *clip_run(c_CTask *t, int64_t x, int64_t y, int64_t *len, int64_t *skip)
{
  int64_t n = *len, end, over;
  *skip = 0;
  x += t->m_scroll_x >> 3;
  if (x < 0) { n += x; *skip -= x; x = 0; }
  x += t->m_win_left;
  end = x + n - 1;
  if (end > t->m_win_right) { over = end - t->m_win_right; end -= over; n -= over; }
  if (x < 0) { n += x; *skip -= x; x = 0; }
  if (end + 1 - TEXT_COLS > 0) n -= end + 1 - TEXT_COLS;
  if (n < 1) return NULL;
  y += t->m_scroll_y >> 3;
  if (y < 0) return NULL;
  y += t->m_win_top;
  if (y > t->m_win_bottom || y < 0 || y >= TEXT_ROWS) return NULL;
  *len = n;
  return h_gr.m_text_base + y * TEXT_COLS + x;
}

int8_t h_TextChar(c_CTask *t, int8_t allow_border, int64_t x, int64_t y, int64_t d)
{
  int64_t b = allow_border ? 1 : 0;
  x += t->m_scroll_x >> 3;
  y += t->m_scroll_y >> 3;
  if (x < -b || y < -b) return 0;
  x += t->m_win_left;
  y += t->m_win_top;
  if (x > t->m_win_right + b || x < 0 || x >= TEXT_COLS) return 0;
  if (y > t->m_win_bottom + b || y < 0 || y >= TEXT_ROWS) return 0;
  h_gr.m_text_base[y * TEXT_COLS + x] = (uint32_t)d;
  return 1;
}

int8_t h_TextLenStr(c_CTask *t, int64_t x, int64_t y, int64_t len, int64_t attr, uint8_t *s)
{
  int64_t skip;
  uint32_t *dst = clip_run(t, x, y, &len, &skip);
  if (!dst) return 0;
  s += skip;
  while (len--) *dst++ = ((uint32_t)attr & ~0xFFu) | *s++;
  return 1;
}

int8_t h_TextLenAttrStr(c_CTask *t, int64_t x, int64_t y, int64_t len, uint32_t *_attr)
{
  int64_t skip;
  uint32_t *dst = clip_run(t, x, y, &len, &skip);
  if (!dst) return 0;
  memcpy(dst, _attr + skip, (size_t)len * 4);
  return 1;
}

int8_t h_TextLenAttr(c_CTask *t, int64_t x, int64_t y, int64_t len, int64_t attr)
{//fills empty cells (char byte 0) until the first non-empty one
  int64_t skip;
  uint32_t *dst = clip_run(t, x, y, &len, &skip);
  if (!dst) return 0;
  for (; len && !(*dst & 0xFF); len--) *dst++ = (uint32_t)attr;
  return 1;
}

int8_t h_IsPixCovered0(c_CTask *t, int64_t x, int64_t y)
{
  return t->m_win_z_num < h_gr.m_win_z_buf[(y >> 3) * TEXT_COLS + (x >> 3)];
}

void h_GrRopEquU8NoClipping(int64_t ch, uint8_t *dst, int64_t w1)
{//8x8 glyph of ch.u8[0] in color ch.u8[1] onto an 8bpp buffer, rows w1 apart
  uint64_t rows = h_text.m_font[ch & 0xFF], c, m, d;
  if ((ch >> ATTRf_UNDERLINE) & 1) rows |= 0xFF00000000000000ull;
  if (!rows) return;
  c = (uint64_t)h_gr.m_to_8_colors[(ch >> 8) & 0xFF];
  for (int i = 0; i < 8; i++, rows >>= 8, dst += w1) {
    m = (uint64_t)h_gr.m_to_8_bits[rows & 0xFF];
    memcpy(&d, dst, 8);
    d = (d & ~m) | (c & m);
    memcpy(dst, &d, 8);
  }
}
