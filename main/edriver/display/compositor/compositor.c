#include "includes.h"
#ifdef EOS_DRV_DISPLAY_COMPOSITOR_ENABLED

#include "ecore/dev.h"
#include "ecore/driver.h"
#include "ecore/ioctl.h"
#include "ecore/log.h"
#include "emisc/types.h"
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  uint32_t x;
  uint32_t y;
  uint32_t w;
  uint32_t h;
  eos_display_rect_t window;
  bool window_set;
} comp_state_t;

bool driver_display_compositor_init(eos_dev_t *dev) {
  if (!dev || !dev->parent) {
    EOS_LOGE("compositor: parent display screen device required");
    errno = EINVAL;
    return false;
  }

  uint32_t parent_w = 0, parent_h = 0;
  eos_dev_ioctl(dev->parent, EOS_DRV_FD, EOS_DISPLAY_IOCTL_GET_WIDTH, &parent_w);
  eos_dev_ioctl(dev->parent, EOS_DRV_FD, EOS_DISPLAY_IOCTL_GET_HEIGHT, &parent_h);
  if (parent_w == 0) parent_w = EOS_DISPLAY_DEFAULT_WIDTH;
  if (parent_h == 0) parent_h = EOS_DISPLAY_DEFAULT_HEIGHT;

  int32_t x = eos_cfg_get_i(dev->cfg, "x", 0);
  int32_t y = eos_cfg_get_i(dev->cfg, "y", 0);
  int32_t w = eos_cfg_get_i(dev->cfg, "w", (int32_t)parent_w);
  int32_t h = eos_cfg_get_i(dev->cfg, "h", (int32_t)parent_h);

  if (x < 0) x = 0;
  if (y < 0) y = 0;
  if (x >= (int32_t)parent_w) x = (int32_t)parent_w - 1;
  if (y >= (int32_t)parent_h) y = (int32_t)parent_h - 1;
  if (x + w > (int32_t)parent_w) w = (int32_t)parent_w - x;
  if (y + h > (int32_t)parent_h) h = (int32_t)parent_h - y;
  if (w <= 0) w = 1;
  if (h <= 0) h = 1;

  comp_state_t *st = calloc(1, sizeof(comp_state_t));
  if (!st) {
    errno = ENOMEM;
    return false;
  }

  st->x = (uint32_t)x;
  st->y = (uint32_t)y;
  st->w = (uint32_t)w;
  st->h = (uint32_t)h;
  st->window.x = 0;
  st->window.y = 0;
  st->window.w = (uint32_t)w;
  st->window.h = (uint32_t)h;
  st->window_set = false;

  dev->state = st;
  return true;
}

int driver_display_compositor_ioctl(eos_dev_t *dev, int fd, int cmd, va_list args) {
  (void)fd;
  comp_state_t *st = (comp_state_t *)dev->state;
  if (!st || !dev->parent) {
    errno = EINVAL;
    return -1;
  }

  switch (cmd) {
  case EOS_DISPLAY_IOCTL_GET_WIDTH: {
    uint32_t *width = va_arg(args, uint32_t *);
    if (!width) { errno = EINVAL; return -1; }
    *width = st->w;
    return 0;
  }

  case EOS_DISPLAY_IOCTL_GET_HEIGHT: {
    uint32_t *height = va_arg(args, uint32_t *);
    if (!height) { errno = EINVAL; return -1; }
    *height = st->h;
    return 0;
  }

  case EOS_DISPLAY_IOCTL_GET_OFFSET_X: {
    uint32_t *ox = va_arg(args, uint32_t *);
    if (!ox) { errno = EINVAL; return -1; }
    *ox = st->x;
    return 0;
  }

  case EOS_DISPLAY_IOCTL_GET_OFFSET_Y: {
    uint32_t *oy = va_arg(args, uint32_t *);
    if (!oy) { errno = EINVAL; return -1; }
    *oy = st->y;
    return 0;
  }

  case EOS_DISPLAY_IOCTL_SET_WINDOW: {
    const eos_display_rect_t *r = va_arg(args, const eos_display_rect_t *);
    if (!r) { errno = EINVAL; return -1; }
    int32_t rx = (int32_t)r->x;
    int32_t ry = (int32_t)r->y;
    int32_t rw = (int32_t)r->w;
    int32_t rh = (int32_t)r->h;

    if (rx < 0) { rw += rx; rx = 0; }
    if (ry < 0) { rh += ry; ry = 0; }
    if (rx + rw > (int32_t)st->w) rw = (int32_t)st->w - rx;
    if (ry + rh > (int32_t)st->h) rh = (int32_t)st->h - ry;
    if (rw < 0) rw = 0;
    if (rh < 0) rh = 0;

    st->window.x = (uint32_t)rx;
    st->window.y = (uint32_t)ry;
    st->window.w = (uint32_t)rw;
    st->window.h = (uint32_t)rh;
    st->window_set = true;
    return 0;
  }

  case EOS_DISPLAY_IOCTL_GET_WINDOW: {
    eos_display_rect_t *r = va_arg(args, eos_display_rect_t *);
    if (!r) { errno = EINVAL; return -1; }
    *r = st->window;
    return 0;
  }

  case EOS_DISPLAY_IOCTL_FLUSH: {
    eos_dev_ioctl(dev->parent, EOS_DRV_FD, EOS_DISPLAY_IOCTL_LOCK);
    int ret = eos_dev_ioctl(dev->parent, EOS_DRV_FD, EOS_DISPLAY_IOCTL_FLUSH);
    eos_dev_ioctl(dev->parent, EOS_DRV_FD, EOS_DISPLAY_IOCTL_UNLOCK);
    return ret;
  }

  case EOS_DISPLAY_IOCTL_LOCK:
    return eos_dev_ioctl(dev->parent, EOS_DRV_FD, EOS_DISPLAY_IOCTL_LOCK);

  case EOS_DISPLAY_IOCTL_UNLOCK:
    return eos_dev_ioctl(dev->parent, EOS_DRV_FD, EOS_DISPLAY_IOCTL_UNLOCK);

  default:
    errno = ENOTTY;
    return -1;
  }
}

ssize_t driver_display_compositor_write(eos_dev_t *dev, int fd, const void *data, size_t size) {
  (void)fd;
  comp_state_t *st = (comp_state_t *)dev->state;
  if (!st || !dev->parent) {
    errno = EINVAL;
    return -1;
  }

  uint32_t wx = st->window_set ? st->window.x : 0;
  uint32_t wy = st->window_set ? st->window.y : 0;
  uint32_t ww = st->window_set ? st->window.w : st->w;
  uint32_t wh = st->window_set ? st->window.h : st->h;

  eos_display_rect_t parent_rect = {
    .x = st->x + wx,
    .y = st->y + wy,
    .w = ww,
    .h = wh
  };

  eos_dev_ioctl(dev->parent, EOS_DRV_FD, EOS_DISPLAY_IOCTL_LOCK);
  eos_dev_ioctl(dev->parent, EOS_DRV_FD, EOS_DISPLAY_IOCTL_SET_WINDOW, &parent_rect);
  ssize_t ret = eos_drv_write(dev->parent, EOS_DRV_FD, data, size);
  eos_dev_ioctl(dev->parent, EOS_DRV_FD, EOS_DISPLAY_IOCTL_UNLOCK);

  st->window_set = false;
  return ret;
}

void driver_display_compositor_shutdown(eos_dev_t *dev) {
  if (dev && dev->state) {
    free(dev->state);
    dev->state = NULL;
  }
}

EOS_DRV_ATTR eos_drv_t driver_display_compositor = {
    EOS_DRV_INIT,
    .scope = "display",
    .name = "compositor",
    .devname = "vfb",
    .init = driver_display_compositor_init,
    .write = driver_display_compositor_write,
    .ioctl = driver_display_compositor_ioctl,
    .shutdown = driver_display_compositor_shutdown};

EOS_DRV_ATTR eos_drv_t driver_display_vfb = {
    EOS_DRV_INIT,
    .scope = "display",
    .name = "vfb",
    .devname = "vfb",
    .init = driver_display_compositor_init,
    .write = driver_display_compositor_write,
    .ioctl = driver_display_compositor_ioctl,
    .shutdown = driver_display_compositor_shutdown};

#endif
