#pragma once
///////////////////////////////////////////////////////
// EOS Project header file
///////////////////////////////////////////////////////

#include "includes.h"
#include "emisc/strlimits.h"
#include "emisc/types.h"
#include "emisc/fancytree.h"

///////////////////////////////////////////////////////
// Structure representing device in EOS dev tree
///////////////////////////////////////////////////////

typedef struct eos_driver_t eos_driver_t;
typedef struct eos_dev_t eos_dev_t;
struct eos_dev_t {
  // Driver to be used to control device
  eos_driver_t *driver;

  // Device pinmap
  eos_pin_t *pins; // {"miso", 13}, {"mosi", 11}, {NULL, 0}

  // Device config
  eos_cfg_t *cfg; // {"speed", "80000000"}, {NULL, NULL}
  // Device state maintained by driver
  void *state;

  // Device tree pointers
  eos_dev_t *parent;
  eos_dev_t *child;
  eos_dev_t *next;

  // Indicates if device slot is in use
  bool in_use;

  // devfs
  uint32_t id;

  // devfs
  char name[EOS_SMALL_STR_LEN];

  // devfs
  int fd;
};

// Generates eos_dev_t_tree_{attach,detach,detach_subtree,walk} -
// see emisc/fancytree.h for what each one does. eos_dev_attach/detach
// below wrap these with driver-specific permission checks and
// id/name assignment.
EOS_TREE_DECLARE(eos_dev_t);

// Mostly for devfs usage. Do not use for other purposes
extern eos_dev_t eos_devices[EOS_MAX_DEVICES];

// Initializes device tree
void eos_devtree_init();

// Returns a pointer to root device
eos_dev_t *eos_devtree_root();

// Allocates a new device or returns NULL in case of error
// and sets eos_errno value
eos_dev_t *eos_dev_alloc();

// Attaches device to EOS device tree
eos_error_t eos_dev_attach(eos_dev_t *dev, eos_dev_t *parent);

// Detaches device and all it's childs from EOS device tree
eos_error_t eos_dev_detach(eos_dev_t *dev);

// inter-driver ioctl communication
int eos_dev_ioctl_call(eos_dev_t *dev, int cmd, ...);
