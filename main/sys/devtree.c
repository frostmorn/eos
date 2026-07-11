#include "devtree.h"
#include "drivers/bus/bus.h"
#include "sys/driver.h"

#define EOS_ROOT_DEV eos_devices[0]

eos_dev_t eos_devices[EOS_MAX_DEVICES];

void eos_devtree_init() {
  // Inital cleanup
  bzero(eos_devices, sizeof(eos_devices));

  // Setup root device
  EOS_ROOT_DEV.driver = eos_driver_find("bus", "root");
  EOS_ROOT_DEV.in_use = true;
}

eos_dev_t *eos_devtree_root() { return &EOS_ROOT_DEV; }

eos_dev_t *eos_dev_alloc() {
  // Find empty device slot
  for (size_t i = 1; i < EOS_MAX_DEVICES; i++)
    if (eos_devices[i].in_use == false) {
      // Prepare slot for usage
      bzero(&eos_devices[i], sizeof(eos_dev_t));
      eos_devices[i].in_use = true;

      return &eos_devices[i];
    }

  // No slot found
  eos_errno = EOS_ERR_DEVICE_COUNT_QUOTA_EXCEED;
  return NULL;
}

// Attaches device to EOS device tree
eos_error_t eos_dev_attach(eos_dev_t *dev, eos_dev_t *dev_bus) {
  // Args check
  if (dev == NULL || (!dev->in_use))
    return EOS_ERR_DEVICE_INVALID;
  if (dev_bus == NULL || (!dev_bus->in_use))
    return EOS_ERR_DEVICE_BUS_INVALID;
  if (dev == dev_bus)
    return EOS_ERR_DEVICE_INVALID;

  // Check if device already attached
  if (dev->parent != NULL)
    return EOS_ERR_DEVICE_ALREADY_ATTACHED;

  // Inform bus through ioctl or other way about new device
  bool attachmentAllowed =
      dev_bus->driver->ioctl(dev_bus, EOS_BUS_IOCTL_KID_ATTACH, dev);

  // Skip if bus not allowed attachment
  if (!attachmentAllowed)
    return EOS_ERR_DEVICE_ATTACH_DECLINED_BY_BUS;

  // Preparing device for an attachment
  dev->parent = dev_bus;
  dev->next = NULL;

  // Seek for a correct place
  if (dev_bus->child == NULL) {
    dev_bus->child = dev;
  } else {
    eos_dev_t *cur = dev_bus->child;

    while (cur->next != NULL)
      cur = cur->next;

    cur->next = dev;
  }

  // Launch driver
  dev->driver->init(dev);

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
  bool detachAllowed =
      dev->parent->driver->ioctl(dev->parent, EOS_BUS_IOCTL_KID_DETACH, dev);

  if (!detachAllowed)
    return EOS_ERR_DEVICE_DETACH_DECLINED_BY_BUS;

  // Recursively detach all children first
  eos_dev_t *child = dev->child;
  while (child != NULL) {
    eos_dev_t *next = child->next;
    eos_dev_detach(child);
    child = next;
  }

  // Shutdown device driver
  dev->driver->shutdown(dev);

  // Unlink from parent's child chain
  if (dev->parent->child == dev) {
    dev->parent->child = dev->next;
  } else {
    eos_dev_t *cur = dev->parent->child;
    while (cur->next != NULL && cur->next != dev)
      cur = cur->next;
    if (cur->next == dev)
      cur->next = dev->next;
  }

  // Cleanup
  bzero(dev, sizeof(eos_dev_t));

  return EOS_ERR_NO_ERROR;
}