#include "dev.h"
#include "ecore/driver.h"
#include <errno.h>
#include <stdlib.h>

// Generates eos_dev_t_tree_attach/detach/detach_subtree/walk,
// operating on this struct's parent/child/next fields (see
// ecore/tree.h - the same generated logic backs application
// contexts).
EOS_TREE_DEFINE(eos_dev_t);

#define EOS_ROOT_DEV eos_devices[0]

EXT_RAM_BSS_ATTR eos_dev_t eos_devices[EOS_MAX_DEVICES];

// Assings per scope id to device
static bool eos_dev_id_used(eos_dev_t *dev, uint32_t id) {
  for (uint32_t i = 0; i < EOS_MAX_DEVICES; i++) {
    eos_dev_t *other = &eos_devices[i];

    if (!other->in_use || other == dev)
      continue;

    if (other->id != id)
      continue;

    if (strcmp(other->driver->devname, dev->driver->devname) != 0)
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
  // Inital cleanup
  bzero(eos_devices, sizeof(eos_devices));

  // Setup root device
  EOS_ROOT_DEV.driver = eos_drv_find("bus", "root");
  EOS_ROOT_DEV.in_use = true;
  eos_dev_assign_id(&EOS_ROOT_DEV);
  eos_dev_assign_name(&EOS_ROOT_DEV);
}

eos_dev_t *eos_devtree_root() { return &EOS_ROOT_DEV; }

eos_dev_t *eos_dev_alloc() {
  // Find empty device slot
  for (size_t i = 1; i < EOS_MAX_DEVICES; i++)
    if (eos_devices[i].in_use == false) {
      // Prepare slot for usage
      bzero(&eos_devices[i], sizeof(eos_dev_t));
      eos_devices[i].in_use = true;
      eos_devices[i].fd = -1; // indicates not in use

      return &eos_devices[i];
    }

  // No slot found
  errno = ENFILE;
  return NULL;
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

// Attaches device to EOS device tree
int eos_dev_attach(eos_dev_t *dev, eos_dev_t *parent) {
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
    for (size_t i = 0; i < EOS_MAX_DEVICES; i++) {
      if (eos_devices[i].in_use && eos_devices[i].driver == dev->driver && &eos_devices[i] != dev) {
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
  for (size_t i = 0; i < EOS_MAX_DEVICES; i++) {
    if (eos_devices[i].in_use && &eos_devices[i] != dev &&
        eos_devices[i].name[0] != '\0' &&
        strcmp(eos_devices[i].name, dev->name) == 0) {
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

// Detaches device and all it's childs from EOS device tree
int eos_dev_detach(eos_dev_t *dev) {
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
    eos_dev_detach(child);
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

eos_dev_t *eos_dev_find_by_name(const char *name){
  // Obviously invalid params
  if (!name || name[0] == '\0') return NULL;

  for (int i = 0; i < EOS_MAX_DEVICES; i++){
    if (strcmp(eos_devices[i].name, name) == 0)
      return &(eos_devices[i]);
    if (strcmp(name, "i2c0") == 0 && strcmp(eos_devices[i].name, "i2c") == 0)
      return &(eos_devices[i]);
    if (strcmp(name, "gpio0") == 0 && strcmp(eos_devices[i].name, "gpio") == 0)
      return &(eos_devices[i]);
    if (strcmp(name, "wifi") == 0 && strcmp(eos_devices[i].name, "wifi0") == 0)
      return &(eos_devices[i]);
    if (strcmp(name, "wifi0") == 0 && strcmp(eos_devices[i].name, "wifi") == 0)
      return &(eos_devices[i]);
    if (strcmp(name, "sta") == 0 && strcmp(eos_devices[i].name, "sta0") == 0)
      return &(eos_devices[i]);
    if (strcmp(name, "sta0") == 0 && strcmp(eos_devices[i].name, "sta") == 0)
      return &(eos_devices[i]);
    if (strcmp(name, "ap") == 0 && strcmp(eos_devices[i].name, "ap0") == 0)
      return &(eos_devices[i]);
    if (strcmp(name, "ap0") == 0 && strcmp(eos_devices[i].name, "ap") == 0)
      return &(eos_devices[i]);
    if (strcmp(name, "br") == 0 && strcmp(eos_devices[i].name, "br0") == 0)
      return &(eos_devices[i]);
    if (strcmp(name, "br0") == 0 && strcmp(eos_devices[i].name, "br") == 0)
      return &(eos_devices[i]);
  }
  return NULL;
}

// Bridges the va_list-based driver->ioctl() signature back to a normal
// call site. devfs_ioctl() gets its va_list "for free" from the VFS
// syscall layer; internal driver-to-driver calls (e.g. a partition
// asking its parent for its sector size) don't have that, so they need
// a small variadic shim to open one.
// TODO: should be a part of ecore/driver ? 
int eos_dev_ioctl_call(eos_dev_t *dev, int fd, int cmd, ...) {
  va_list args;
  va_start(args, cmd);
  int ret = eos_drv_ioctl(dev, fd, cmd, args);
  va_end(args);
  return ret;
}
