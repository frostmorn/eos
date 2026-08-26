#include "dev.h"
#include "ecore/driver.h"
#include "ecore/ioctl.h"

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
  if (dev->driver->devname[0] != '\0')
    snprintf(dev->name, EOS_SMALL_STR_LEN, "%s%" PRIu32, dev->driver->devname,
             dev->id);
  else
    dev->name[0] = '\0';
}

void eos_devtree_init() {
  // Inital cleanup
  bzero(eos_devices, sizeof(eos_devices));

  // Setup root device
  EOS_ROOT_DEV.driver = eos_driver_find("bus", "root");
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
  eos_errno = EOS_ERR_DEVICE_COUNT_QUOTA_EXCEED;
  return NULL;
}

// Attaches device to EOS device tree
eos_error_t eos_dev_attach(eos_dev_t *dev, eos_dev_t *parent) {
  // Args check
  if (dev == NULL || (!dev->in_use))
    return EOS_ERR_DEVICE_INVALID;
  if (parent == NULL || (!parent->in_use))
    return EOS_ERR_DEVICE_PARENT_INVALID;
  if (dev == parent)
    return EOS_ERR_DEVICE_INVALID;

  // Check if device already attached
  if (dev->parent != NULL)
    return EOS_ERR_DEVICE_ALREADY_ATTACHED;

  // Inform bus through ioctl or other way about new device
  bool attachmentAllowed = parent->driver->attach_req(parent, dev);

  // Skip if bus not allowed attachment
  if (!attachmentAllowed)
    return EOS_ERR_DEVICE_ATTACH_DECLINED;

  // Attach to the device tree as parent's newest child (appended
  // after any existing children, so devfs enumeration order still
  // matches attach order).
  eos_dev_t_tree_attach(dev, parent);

  // Assign device id
  eos_dev_assign_id(dev);

  // Assign device name for devfs path buildage
  eos_dev_assign_name(dev);

  // Launch driver
  if (!dev->driver->init(dev)) {
    eos_dev_detach(dev);
    return EOS_ERR_DEVICE_INIT_FAILED;
  }

  return EOS_ERR_NO_ERROR;
}

// Detaches device and all it's childs from EOS device tree
eos_error_t eos_dev_detach(eos_dev_t *dev) {
  if (dev == NULL || (!dev->in_use))
    return EOS_ERR_DEVICE_INVALID;

  // Already detached (or root node)
  if (dev->parent == NULL)
    return EOS_ERR_NO_ERROR;

  // Ask bus permission to detach device
  bool detachAllowed = dev->parent->driver->detach_req(dev->parent, dev);

  if (!detachAllowed)
    return EOS_ERR_DEVICE_DETACH_DECLINED;

  // Recursively detach all children first
  eos_dev_t *child = dev->child;
  while (child != NULL) {
    eos_dev_t *next = child->next;
    eos_dev_detach(child);
    child = next;
  }

  // Shutdown device driver
  dev->driver->shutdown(dev);

  // Unlink dev from the tree. Any child that refused detach_req above
  // (still attached at this point) gets reparented onto dev->parent
  // instead of being left with a dangling pointer into dev's slot,
  // which is about to be zeroed and made available for reuse.
  eos_dev_t_tree_detach(dev);

  // Cleanup
  bzero(dev, sizeof(eos_dev_t));

  return EOS_ERR_NO_ERROR;
}

// Bridges the va_list-based driver->ioctl() signature back to a normal
// call site. devfs_ioctl() gets its va_list "for free" from the VFS
// syscall layer; internal driver-to-driver calls (e.g. a partition
// asking its parent for its sector size) don't have that, so they need
// a small variadic shim to open one.
int eos_dev_ioctl_call(eos_dev_t *dev, int cmd, ...) {
  if (!dev || !dev->driver || !dev->driver->ioctl)
    return EOS_ERR_NOT_SUPPORTED;

  va_list args;
  va_start(args, cmd);
  int ret = dev->driver->ioctl(dev, cmd, args);
  va_end(args);
  return ret;
}

