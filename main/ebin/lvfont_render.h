#pragma once

#include "ecore/lvgl.h"
#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static inline uint32_t lvfont_decode_utf8(const char **p) {
  if (!p || !*p || !**p) return 0;
  const uint8_t *s = (const uint8_t *)*p;
  uint32_t cp = 0;
  if (s[0] < 0x80) {
    cp = s[0];
    *p += 1;
  } else if ((s[0] & 0xE0) == 0xC0) {
    if ((s[1] & 0xC0) == 0x80) {
      cp = ((s[0] & 0x1F) << 6) | (s[1] & 0x3F);
      *p += 2;
    } else {
      *p += 1;
    }
  } else if ((s[0] & 0xF0) == 0xE0) {
    if ((s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80) {
      cp = ((s[0] & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
      *p += 3;
    } else {
      *p += 1;
    }
  } else if ((s[0] & 0xF8) == 0xF0) {
    if ((s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80 && (s[3] & 0xC0) == 0x80) {
      cp = ((s[0] & 0x07) << 18) | ((s[1] & 0x3F) << 12) | ((s[2] & 0x3F) << 6) |
           (s[3] & 0x3F);
      *p += 4;
    } else {
      *p += 1;
    }
  } else {
    *p += 1;
  }
  return cp;
}

static inline uint32_t lvfont_measure_text(const lv_font_t *font, const char *text) {
  if (!font || !text) return 0;
  const char *p = text;
  uint32_t w = 0;
  while (*p) {
    uint32_t cp = lvfont_decode_utf8(&p);
    if (!cp) break;
    // Map geometric arrows to FontAwesome symbols in Montserrat
    if (cp == 0x25B2) cp = 0xF077;       // ▲ -> LV_SYMBOL_UP
    else if (cp == 0x25BC) cp = 0xF078;  // ▼ -> LV_SYMBOL_DOWN
    else if (cp == 0x25C4) cp = 0xF053;  // ◄ -> LV_SYMBOL_LEFT
    else if (cp == 0x25BA) cp = 0xF054;  // ► -> LV_SYMBOL_RIGHT

    lv_font_glyph_dsc_t dsc;
    memset(&dsc, 0, sizeof(dsc));
    if (lv_font_get_glyph_dsc(font, &dsc, cp, 0)) {
      w += dsc.adv_w;
    }
  }
  return w;
}

static inline void lvfont_render_text(uint16_t *buf, uint32_t buf_w, uint32_t buf_h,
                                      const lv_font_t *font, const char *text,
                                      uint16_t fg_color, int32_t start_x, int32_t base_y) {
  if (!buf || !font || !text || buf_w == 0 || buf_h == 0) return;
  const char *p = text;
  int32_t cur_x = start_x;

  uint32_t r_fg = (fg_color >> 11) & 0x1F;
  uint32_t g_fg = (fg_color >> 5) & 0x3F;
  uint32_t b_fg = fg_color & 0x1F;

  while (*p && cur_x < (int32_t)buf_w) {
    uint32_t cp = lvfont_decode_utf8(&p);
    if (!cp) break;

    // Map geometric arrows to FontAwesome symbols in Montserrat
    if (cp == 0x25B2) cp = 0xF077;       // ▲ -> LV_SYMBOL_UP
    else if (cp == 0x25BC) cp = 0xF078;  // ▼ -> LV_SYMBOL_DOWN
    else if (cp == 0x25C4) cp = 0xF053;  // ◄ -> LV_SYMBOL_LEFT
    else if (cp == 0x25BA) cp = 0xF054;  // ► -> LV_SYMBOL_RIGHT

    lv_font_glyph_dsc_t dsc;
    memset(&dsc, 0, sizeof(dsc));
    if (!lv_font_get_glyph_dsc(font, &dsc, cp, 0)) {
      continue;
    }

    dsc.req_raw_bitmap = 1;
    const uint8_t *raw = font->get_glyph_bitmap(&dsc, NULL);
    if (raw && dsc.box_w > 0 && dsc.box_h > 0) {
      int32_t gx = cur_x + dsc.ofs_x;
      int32_t gy = base_y - dsc.ofs_y - dsc.box_h;

      int32_t shift = 8;
      uint8_t cur = 0;
      for (int y = 0; y < dsc.box_h; y++) {
        int32_t py = gy + y;
        for (int x = 0; x < dsc.box_w; x++) {
          int32_t px = gx + x;
          uint8_t alpha;
          if (shift == 8) {
            cur = *raw++;
            alpha = (cur >> 4) & 0x0F;
            shift = 4;
          } else {
            alpha = cur & 0x0F;
            shift = 8;
          }

          if (py >= 0 && py < (int32_t)buf_h && px >= 0 && px < (int32_t)buf_w) {
            if (alpha == 15) {
              buf[py * buf_w + px] = fg_color;
            } else if (alpha > 0) {
              uint16_t bg = buf[py * buf_w + px];
              uint32_t r_bg = (bg >> 11) & 0x1F;
              uint32_t g_bg = (bg >> 5) & 0x3F;
              uint32_t b_bg = bg & 0x1F;

              uint32_t inv = 15 - alpha;
              uint32_t r_out = (r_fg * alpha + r_bg * inv + 7) / 15;
              uint32_t g_out = (g_fg * alpha + g_bg * inv + 7) / 15;
              uint32_t b_out = (b_fg * alpha + b_bg * inv + 7) / 15;

              buf[py * buf_w + px] = (uint16_t)((r_out << 11) | (g_out << 5) | b_out);
            }
          }
        }
      }
    }
    cur_x += dsc.adv_w;
  }
}
