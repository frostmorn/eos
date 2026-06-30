#pragma once
///////////////////////////////////////////////////////
// EOS Project header file
///////////////////////////////////////////////////////

#include "includes.h"

///////////////////////////////////////////////////////
// Structure representing device in EOS dev tree
///////////////////////////////////////////////////////

typedef struct eos_driver_t eos_driver_t;
typedef struct eos_dev_t eos_dev_t;
struct eos_dev_t {
  // Driver to be used to control device
  eos_driver_t *driver;
  // Static device config
  void *config;
  // Device state maintained by driver
  void *state;

  // Device tree pointers
  eos_dev_t *parent;
  eos_dev_t *child;
  eos_dev_t *next;
};

extern eos_dev_t eos_root_dev;

// Initializes device tree
void eos_devtree_init();