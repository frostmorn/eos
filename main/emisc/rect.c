#include "emisc/rect.h"
#include <string.h>

bool eos_rect_is_empty(const eos_rect_t *r) {
  return !r || r->w == 0 || r->h == 0;
}

bool eos_rect_equals(const eos_rect_t *a, const eos_rect_t *b) {
  if (a == b) return true;
  if (!a || !b) return false;
  return a->x == b->x && a->y == b->y && a->w == b->w && a->h == b->h;
}

bool eos_rect_intersect(const eos_rect_t *a, const eos_rect_t *b, eos_rect_t *out) {
  if (eos_rect_is_empty(a) || eos_rect_is_empty(b)) {
    if (out) memset(out, 0, sizeof(*out));
    return false;
  }

  uint32_t x0 = (a->x > b->x) ? a->x : b->x;
  uint32_t y0 = (a->y > b->y) ? a->y : b->y;
  uint32_t x1 = ((a->x + a->w) < (b->x + b->w)) ? (a->x + a->w) : (b->x + b->w);
  uint32_t y1 = ((a->y + a->h) < (b->y + b->h)) ? (a->y + a->h) : (b->y + b->h);

  if (x1 > x0 && y1 > y0) {
    if (out) {
      out->x = x0;
      out->y = y0;
      out->w = x1 - x0;
      out->h = y1 - y0;
    }
    return true;
  }

  if (out) memset(out, 0, sizeof(*out));
  return false;
}

bool eos_rect_contains(const eos_rect_t *outer, const eos_rect_t *inner) {
  if (eos_rect_is_empty(outer) || eos_rect_is_empty(inner)) return false;
  return (inner->x >= outer->x) &&
         (inner->y >= outer->y) &&
         (inner->x + inner->w <= outer->x + outer->w) &&
         (inner->y + inner->h <= outer->y + outer->h);
}

bool eos_rect_clip(const eos_rect_t *rect, const eos_rect_t *bounds, eos_rect_t *out) {
  return eos_rect_intersect(rect, bounds, out);
}

size_t eos_rect_subtract(const eos_rect_t *subject, const eos_rect_t *occluder,
                         eos_rect_t out_pieces[4]) {
  if (eos_rect_is_empty(subject)) return 0;
  if (eos_rect_is_empty(occluder)) {
    if (out_pieces) out_pieces[0] = *subject;
    return 1;
  }

  eos_rect_t inter;
  if (!eos_rect_intersect(subject, occluder, &inter)) {
    if (out_pieces) out_pieces[0] = *subject;
    return 1;
  }

  // If occluder completely covers subject
  if (inter.x == subject->x && inter.y == subject->y &&
      inter.w == subject->w && inter.h == subject->h) {
    return 0;
  }

  size_t count = 0;

  // Top piece (spanning full subject width)
  if (inter.y > subject->y) {
    if (out_pieces) {
      out_pieces[count].x = subject->x;
      out_pieces[count].y = subject->y;
      out_pieces[count].w = subject->w;
      out_pieces[count].h = inter.y - subject->y;
    }
    count++;
  }

  // Bottom piece (spanning full subject width)
  uint32_t sub_b = subject->y + subject->h;
  uint32_t int_b = inter.y + inter.h;
  if (sub_b > int_b) {
    if (out_pieces) {
      out_pieces[count].x = subject->x;
      out_pieces[count].y = int_b;
      out_pieces[count].w = subject->w;
      out_pieces[count].h = sub_b - int_b;
    }
    count++;
  }

  // Left piece (between inter.y and inter.y + inter.h)
  if (inter.x > subject->x) {
    if (out_pieces) {
      out_pieces[count].x = subject->x;
      out_pieces[count].y = inter.y;
      out_pieces[count].w = inter.x - subject->x;
      out_pieces[count].h = inter.h;
    }
    count++;
  }

  // Right piece (between inter.y and inter.y + inter.h)
  uint32_t sub_r = subject->x + subject->w;
  uint32_t int_r = inter.x + inter.w;
  if (sub_r > int_r) {
    if (out_pieces) {
      out_pieces[count].x = int_r;
      out_pieces[count].y = inter.y;
      out_pieces[count].w = sub_r - int_r;
      out_pieces[count].h = inter.h;
    }
    count++;
  }

  return count;
}

#define EOS_RECT_MAX_TEMP 64

size_t eos_rect_multi_subtract(const eos_rect_t *subject,
                               const eos_rect_t *occluders, size_t occluder_count,
                               eos_rect_t *out_pieces, size_t max_out) {
  if (eos_rect_is_empty(subject) || max_out == 0) return 0;
  if (!occluders || occluder_count == 0) {
    out_pieces[0] = *subject;
    return 1;
  }

  eos_rect_t pool_a[EOS_RECT_MAX_TEMP];
  eos_rect_t pool_b[EOS_RECT_MAX_TEMP];
  eos_rect_t *cur_in = pool_a;
  eos_rect_t *cur_out = pool_b;

  cur_in[0] = *subject;
  size_t in_count = 1;

  for (size_t i = 0; i < occluder_count && in_count > 0; i++) {
    const eos_rect_t *occ = &occluders[i];
    if (eos_rect_is_empty(occ)) continue;

    size_t out_count = 0;
    for (size_t j = 0; j < in_count; j++) {
      eos_rect_t sub_pieces[4];
      size_t n = eos_rect_subtract(&cur_in[j], occ, sub_pieces);
      for (size_t k = 0; k < n; k++) {
        if (out_count < EOS_RECT_MAX_TEMP) {
          cur_out[out_count++] = sub_pieces[k];
        }
      }
    }

    // Swap buffers
    eos_rect_t *tmp = cur_in;
    cur_in = cur_out;
    cur_out = tmp;
    in_count = out_count;
  }

  size_t copy_n = (in_count < max_out) ? in_count : max_out;
  for (size_t i = 0; i < copy_n; i++) {
    out_pieces[i] = cur_in[i];
  }

  return copy_n;
}
