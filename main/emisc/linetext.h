#pragma once
///////////////////////////////////////////////////////
// EOS Project header file
///////////////////////////////////////////////////////
// Pure line text & icon renderer into RGB565 buffers
// with zero OS dependencies.
///////////////////////////////////////////////////////

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define EOS_LINETEXT_FONT_W 8
#define EOS_LINETEXT_FONT_H 12

// Measures rendered pixel width of a UTF-8 string
uint32_t eos_linetext_measure(const char *utf8_text);

// Renders UTF-8 text into an RGB565 line buffer (buf_w * buf_h pixels)
// dst_rgb565: destination pixel array of size buf_w * buf_h
// utf8_text: null-terminated UTF-8 string (control characters are stripped)
// fg_color: RGB565 foreground text color
// bg_color: RGB565 background color
// center: if true, text is centered within (buf_w - 2 * inset_x)
// inset_x: horizontal padding from left/right margins
void eos_linetext_render(uint16_t *dst_rgb565, uint32_t buf_w, uint32_t buf_h,
                         const char *utf8_text, uint16_t fg_color, uint16_t bg_color,
                         bool center, uint32_t inset_x);

// Renders UTF-8 text at a specific vertical y offset without modifying background pixels
void eos_linetext_render_at(uint16_t *dst_rgb565, uint32_t buf_w, uint32_t buf_h,
                            const char *utf8_text, uint16_t fg_color,
                            bool center, uint32_t inset_x, int32_t start_y);
