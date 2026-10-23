#include "dev.h"
#include "ecore/driver.h"
#include <errno.h>

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

// Attaches device to EOS device tree
int eos_dev_attach(eos_dev_t *dev, eos_dev_t *parent) {
  // Args check
  if (dev == NULL || (!dev->in_use)) {
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
