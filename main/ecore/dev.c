#include "dev.h"
#include "ecore/driver.h"
#include "ecore/proc.h"
#include <errno.h>
#include <stdlib.h>

// Generates eos_dev_t_tree_attach/detach/detach_subtree/walk,
// operating on this struct's parent/child/next fields (see
// ecore/tree.h - the same generated logic backs application
// contexts).
EOS_TREE_DEFINE(eos_dev_t);

static kvec_psram_t(eos_dev_t *) s_devices;

size_t eos_dev_count(void) {
  return kv_size(s_devices);
}

eos_dev_t *eos_dev_get(size_t index) {
  if (index >= kv_size(s_devices))
    return NULL;
  return kv_A(s_devices, index);
}

// Assings per scope id to device
static bool eos_dev_id_used(eos_dev_t *dev, uint32_t id) {
  for (size_t i = 0; i < kv_size(s_devices); i++) {
    eos_dev_t *other = kv_A(s_devices, i);

    if (!other || !other->in_use || other == dev)
      continue;

    if (other->id != id)
      continue;

    if (!other->driver || !dev->driver ||
        strcmp(other->driver->devname, dev->driver->devname) != 0)
      continue;

    return true;
  }

  return false;
}

void eos_dev_assign_id(eos_dev_t *dev) {
  for (uint32_t id = 0;; id++) {
    if (!eos_dev_id_used(dev, id)) {
      dev->id = id;
      return;
    }
  }
}

void eos_dev_assign_name(eos_dev_t *dev) {
  if (dev->driver->devname[0] != '\0'){
    if (dev->driver->flags & EOS_DRV_FLAG_NO_INDEX){
      strcpy(dev->name, dev->driver->devname);
    }
    else{
      snprintf(dev->name, EOS_SMALL_STR_LEN, "%s%" PRIu32, dev->driver->devname,
        dev->id);
    }
  }
  else
    dev->name[0] = '\0';
}

void eos_devtree_init() {
  bool prev_h = eos_proc_heap_tracking_disable();
  kv_init(s_devices);

  // Setup root device in PSRAM heap
  eos_dev_t *root = (eos_dev_t *)eos_psram_calloc_prefer(1, sizeof(eos_dev_t));
  if (!root) {
    root = (eos_dev_t *)calloc(1, sizeof(eos_dev_t));
  }
  root->driver = eos_drv_find("bus", "root");
  root->in_use = true;
  root->fd = -1;
  kv_push(eos_dev_t *, s_devices, root);
  eos_dev_assign_id(root);
  eos_dev_assign_name(root);
  eos_proc_heap_tracking_restore(prev_h);
}

eos_dev_t *eos_devtree_root() {
  if (kv_size(s_devices) == 0) return NULL;
  return kv_A(s_devices, 0);
}

eos_dev_t *eos_dev_alloc() {
  bool prev_h = eos_proc_heap_tracking_disable();
  // Find empty reused device slot (slot 0 is reserved for root)
  for (size_t i = 1; i < kv_size(s_devices); i++) {
    eos_dev_t *dev = kv_A(s_devices, i);
    if (dev && !dev->in_use) {
      memset(dev, 0, sizeof(eos_dev_t));
      dev->in_use = true;
      dev->fd = -1; // indicates not in use
      eos_proc_heap_tracking_restore(prev_h);
      return dev;
    }
  }

  // Allocate new slot dynamically in PSRAM heap
  eos_dev_t *dev = (eos_dev_t *)eos_psram_calloc_prefer(1, sizeof(eos_dev_t));
  if (!dev) {
    dev = (eos_dev_t *)calloc(1, sizeof(eos_dev_t));
  }
  if (!dev) {
    eos_proc_heap_tracking_restore(prev_h);
    errno = ENOMEM;
    return NULL;
  }
  dev->in_use = true;
  dev->fd = -1;
  kv_push(eos_dev_t *, s_devices, dev);
  eos_proc_heap_tracking_restore(prev_h);
  return dev;
}

static bool eos_dev_is_same_device(eos_dev_t *existing, eos_dev_t *dev) {
  if (!existing || !dev || !existing->driver || !dev->driver)
    return false;

  // Different driver means different device type
  if (existing->driver != dev->driver)
    return false;

  // If driver has EOS_DRV_FLAG_NO_INDEX, it cannot have more than 1 instance
  if (dev->driver->flags & EOS_DRV_FLAG_NO_INDEX)
    return true;

  // Check bus/sdio: distinguished by slot
  if (strcmp(dev->driver->scope, "bus") == 0 && strcmp(dev->driver->name, "sdio") == 0) {
    int s1 = eos_cfg_get_i(existing->cfg, "slot", 0);
    int s2 = eos_cfg_get_i(dev->cfg, "slot", 0);
    return (s1 == s2);
  }

  // Check bus/spi: distinguished by host
  if (strcmp(dev->driver->scope, "bus") == 0 && strcmp(dev->driver->name, "spi") == 0) {
    int h1 = eos_cfg_get_i(existing->cfg, "host", 1);
    int h2 = eos_cfg_get_i(dev->cfg, "host", 1);
    return (h1 == h2);
  }

  // Check bus/i2c: distinguished by port
  if (strcmp(dev->driver->scope, "bus") == 0 && strcmp(dev->driver->name, "i2c") == 0) {
    int p1 = eos_cfg_get_i(existing->cfg, "port", 0);
    int p2 = eos_cfg_get_i(dev->cfg, "port", 0);
    return (p1 == p2);
  }

  // Check storage/sd: distinguished by cs pin on SPI
  if (strcmp(dev->driver->scope, "storage") == 0 && strcmp(dev->driver->name, "sd") == 0) {
    int32_t cs1 = eos_pin_get_no(existing->pins, "cs");
    int32_t cs2 = eos_pin_get_no(dev->pins, "cs");
    if (cs1 >= 0 && cs2 >= 0) return (cs1 == cs2);
    return true;
  }

  // Check input/gt911: distinguished by I2C address
  if (strcmp(dev->driver->scope, "input") == 0 && strcmp(dev->driver->name, "gt911") == 0) {
    int a1 = eos_cfg_get_i(existing->cfg, "addr", 0x5D);
    int a2 = eos_cfg_get_i(dev->cfg, "addr", 0x5D);
    return (a1 == a2);
  }

  // For all other device drivers attached under the same parent (flash, wifi, sta, ap,
  // gpio_keyboard, max98357 sound, battery, adc, display, bridge):
  // Two instances of the same driver under the same parent represent the same device.
  return true;
}

// Attaches device to EOS device tree (internal implementation)
static int eos_dev_attach_internal(eos_dev_t *dev, eos_dev_t *parent) {
  // Args check
  if (dev == NULL || (!dev->in_use) || dev->driver == NULL) {
    errno = EINVAL;
    return -1;
  }
  if (parent == NULL || (!parent->in_use)) {
    errno = EINVAL;
    return -1;
  }
  if (dev == parent) {
    errno = EINVAL;
    return -1;
  }

  // Check if device already attached
  if (dev->parent != NULL) {
    errno = EEXIST;
    return -1;
  }

  // Check if driver is a singleton without index and already attached anywhere in devtree
  if (dev->driver->flags & EOS_DRV_FLAG_NO_INDEX) {
    for (size_t i = 0; i < kv_size(s_devices); i++) {
      eos_dev_t *d = kv_A(s_devices, i);
      if (d && d->in_use && d->driver == dev->driver && d != dev) {
        errno = EEXIST;
        return -1;
      }
    }
  }

  // Check if the same device is already attached at parent
  for (eos_dev_t *child = parent->child; child != NULL; child = child->next) {
    if (!child->in_use || !child->driver)
      continue;
    if (eos_dev_is_same_device(child, dev)) {
      errno = EEXIST;
      return -1;
    }
  }

  // Inform bus through ioctl or other way about new device
  bool attachmentAllowed = eos_drv_attach_req(parent, dev);

  // Skip if bus not allowed attachment
  if (!attachmentAllowed) {
    errno = EPERM;
    return -1;
  }

  // Attach to the device tree as parent's newest child (appended
  // after any existing children, so devfs enumeration order still
  // matches attach order).
  eos_dev_t_tree_attach(dev, parent);

  // Assign device id
  eos_dev_assign_id(dev);

  // Assign device name for devfs path buildage
  eos_dev_assign_name(dev);

  // Prevent name collisions in devfs
  for (size_t i = 0; i < kv_size(s_devices); i++) {
    eos_dev_t *d = kv_A(s_devices, i);
    if (d && d->in_use && d != dev &&
        d->name[0] != '\0' &&
        strcmp(d->name, dev->name) == 0) {
      eos_dev_t_tree_detach(dev);
      errno = EEXIST;
      return -1;
    }
  }

  // Launch driver
  if (!eos_drv_init(dev)) {
    eos_dev_detach(dev);
    errno = EIO;
    return -1;
  }

  return 0;
}

int eos_dev_attach(eos_dev_t *dev, eos_dev_t *parent) {
  // Adopt dynamic pins and config arrays into kernel context (PID 0)
  // so driver configuration survives calling process exit
  if (dev) {
    if (dev->pins) eos_mem_adopt(dev->pins, 0);
    if (dev->dynamic_pins && dev->pins) {
      for (eos_pin_t *p = dev->pins; p->key != NULL; p++) {
        if (p->key) eos_mem_adopt((void *)p->key, 0);
      }
    }
    if (dev->cfg) eos_mem_adopt(dev->cfg, 0);
    if (dev->dynamic_cfg && dev->cfg) {
      for (eos_cfg_t *c = dev->cfg; c->key != NULL; c++) {
        if (c->key) eos_mem_adopt((void *)c->key, 0);
        if (c->type == EOS_CFG_STR && c->val.s) {
          eos_mem_adopt((void *)c->val.s, 0);
        }
      }
    }
  }

  bool prev_kernel = eos_proc_enter_kernel();
  int res = eos_dev_attach_internal(dev, parent);
  eos_proc_leave_kernel(prev_kernel);
  return res;
}

// Detaches device and all it's childs from EOS device tree (internal implementation)
static int eos_dev_detach_internal(eos_dev_t *dev) {
  if (dev == NULL || (!dev->in_use)) {
    errno = EINVAL;
    return -1;
  }

  // Already detached (or root node)
  if (dev->parent == NULL)
    return 0;

  // Ask bus permission to detach device
  bool detachAllowed = eos_drv_detach_req(dev->parent, dev);

  if (!detachAllowed) {
    errno = EPERM;
    return -1;
  }

  // Recursively detach all children first
  eos_dev_t *child = dev->child;
  while (child != NULL) {
    eos_dev_t *next = child->next;
    eos_dev_detach_internal(child);
    child = next;
  }

  // Shutdown device driver
  eos_drv_shutdown(dev);

  // Unlink dev from the tree. Any child that refused detach_req above
  // (still attached at this point) gets reparented onto dev->parent
  // instead of being left with a dangling pointer into dev's slot,
  // which is about to be zeroed and made available for reuse.
  eos_dev_t_tree_detach(dev);

  // Free dynamically allocated pin and config arrays
  if (dev->dynamic_pins && dev->pins) {
    for (eos_pin_t *p = dev->pins; p->key != NULL; p++) {
      if (p->key) free(p->key);
    }
    free(dev->pins);
  }

  if (dev->dynamic_cfg && dev->cfg) {
    for (eos_cfg_t *c = dev->cfg; c->key != NULL; c++) {
      if (c->key) free((void *)c->key);
      if (c->type == EOS_CFG_STR && c->val.s) {
        free(c->val.s);
      }
    }
    free(dev->cfg);
  }

  // Cleanup
  bzero(dev, sizeof(eos_dev_t));

  return 0;
}

int eos_dev_detach(eos_dev_t *dev) {
  bool prev_kernel = eos_proc_enter_kernel();
  int res = eos_dev_detach_internal(dev);
  eos_proc_leave_kernel(prev_kernel);
  return res;
}

eos_dev_t *eos_dev_find_by_name(const char *name){
  // Obviously invalid params
  if (!name || name[0] == '\0') return NULL;

  for (size_t i = 0; i < kv_size(s_devices); i++){
    eos_dev_t *dev = kv_A(s_devices, i);
    if (!dev || !dev->in_use) continue;
    if (strcmp(dev->name, name) == 0)
      return dev;
    if (strcmp(name, "i2c0") == 0 && strcmp(dev->name, "i2c") == 0)
      return dev;
    if (strcmp(name, "gpio0") == 0 && strcmp(dev->name, "gpio") == 0)
      return dev;
    if (strcmp(name, "wifi") == 0 && strcmp(dev->name, "wifi0") == 0)
      return dev;
    if (strcmp(name, "wifi0") == 0 && strcmp(dev->name, "wifi") == 0)
      return dev;
    if (strcmp(name, "sta") == 0 && strcmp(dev->name, "sta0") == 0)
      return dev;
    if (strcmp(name, "sta0") == 0 && strcmp(dev->name, "sta") == 0)
      return dev;
    if (strcmp(name, "ap") == 0 && strcmp(dev->name, "ap0") == 0)
      return dev;
    if (strcmp(name, "ap0") == 0 && strcmp(dev->name, "ap") == 0)
      return dev;
    if (strcmp(name, "br") == 0 && strcmp(dev->name, "br0") == 0)
      return dev;
    if (strcmp(name, "br0") == 0 && strcmp(dev->name, "br") == 0)
      return dev;
  }
  return NULL;
}

// Device operations on device tree nodes
int eos_dev_open(eos_dev_t *dev, const char *path, int flags, int mode) {
  return eos_drv_open(dev, path, flags, mode);
}

int eos_dev_close(eos_dev_t *dev, int fd) {
  return eos_drv_close(dev, fd);
}

ssize_t eos_dev_read(eos_dev_t *dev, int fd, void *dst, size_t size) {
  return eos_drv_read(dev, fd, dst, size);
}

ssize_t eos_dev_write(eos_dev_t *dev, int fd, const void *data, size_t size) {
  return eos_drv_write(dev, fd, data, size);
}

off_t eos_dev_lseek(eos_dev_t *dev, int fd, off_t offset, int whence) {
  return eos_drv_lseek(dev, fd, offset, whence);
}

ssize_t eos_dev_pread(eos_dev_t *dev, int fd, void *dst, size_t size,
                      off_t offset) {
  return eos_drv_pread(dev, fd, dst, size, offset);
}

ssize_t eos_dev_pwrite(eos_dev_t *dev, int fd, const void *src, size_t size,
                       off_t offset) {
  return eos_drv_pwrite(dev, fd, (void *)src, size, offset);
}

int eos_dev_ioctl(eos_dev_t *dev, int fd, int cmd, ...) {
  va_list args;
  va_start(args, cmd);
  bool prev_k = eos_proc_enter_kernel();
  int ret = eos_drv_ioctl(dev, fd, cmd, args);
  eos_proc_leave_kernel(prev_k);
  va_end(args);
  return ret;
}

int eos_dev_ioctl_call(eos_dev_t *dev, int fd, int cmd, ...) {
  va_list args;
  va_start(args, cmd);
  bool prev_k = eos_proc_enter_kernel();
  int ret = eos_drv_ioctl(dev, fd, cmd, args);
  eos_proc_leave_kernel(prev_k);
  va_end(args);
  return ret;
}

DIR *eos_dev_opendir(eos_dev_t *dev, const char *path) {
  return eos_drv_opendir(dev, path);
}

struct dirent *eos_dev_readdir(eos_dev_t *dev, DIR *pdir) {
  return eos_drv_readdir(dev, pdir);
}

long eos_dev_telldir(eos_dev_t *dev, DIR *pdir) {
  return eos_drv_telldir(dev, pdir);
}

void eos_dev_seekdir(eos_dev_t *dev, DIR *pdir, long offset) {
  eos_drv_seekdir(dev, pdir, offset);
}

int eos_dev_closedir(eos_dev_t *dev, DIR *pdir) {
  return eos_drv_closedir(dev, pdir);
}
