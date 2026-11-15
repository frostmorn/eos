#pragma once
///////////////////////////////////////////////////////
// EOS Project header file
///////////////////////////////////////////////////////
// Pure 2D integer rectangle mathematics and clipping
// utilities with zero OS dependencies.
///////////////////////////////////////////////////////

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
  uint32_t x;
  uint32_t y;
  uint32_t w;
  uint32_t h;
} eos_rect_t;

// Returns true if rectangle has zero area or NULL
bool eos_rect_is_empty(const eos_rect_t *r);

// Returns true if two rectangles are identical
bool eos_rect_equals(const eos_rect_t *a, const eos_rect_t *b);

// Computes intersection of a and b. Returns true if non-empty intersection exists.
bool eos_rect_intersect(const eos_rect_t *a, const eos_rect_t *b, eos_rect_t *out);

// Returns true if outer wholly contains inner
bool eos_rect_contains(const eos_rect_t *outer, const eos_rect_t *inner);

// Clips rect to bounds. Equivalent to eos_rect_intersect(rect, bounds, out).
bool eos_rect_clip(const eos_rect_t *rect, const eos_rect_t *bounds, eos_rect_t *out);

// Subtracts occluder from subject. Produces 0 to 4 non-overlapping rectangles.
// Returns number of resulting pieces.
size_t eos_rect_subtract(const eos_rect_t *subject, const eos_rect_t *occluder,
                         eos_rect_t out_pieces[4]);

// Subtracts an array of occluders from subject.
// Writes resulting non-overlapping visible pieces to out_pieces (up to max_out).
// Returns number of resulting pieces.
size_t eos_rect_multi_subtract(const eos_rect_t *subject,
                               const eos_rect_t *occluders, size_t occluder_count,
                               eos_rect_t *out_pieces, size_t max_out);
