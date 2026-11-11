#pragma once
///////////////////////////////////////////////////////
// EOS Project header file
///////////////////////////////////////////////////////

#include "includes.h"
#include <dirent.h>
#include "emisc/strlimits.h"
#include "emisc/types.h"
#include "emisc/fancytree.h"

///////////////////////////////////////////////////////
// Structure representing device in EOS dev tree
///////////////////////////////////////////////////////

typedef struct eos_drv_t eos_drv_t;
typedef struct eos_dev_t eos_dev_t;
struct eos_dev_t {
  // Driver to be used to control device
  eos_drv_t *driver;

  // Device pinmap
  eos_pin_t *pins; // {"miso", 13}, {"mosi", 11}, {NULL, 0}

  // Device config
  eos_cfg_t *cfg; // {"speed", "80000000"}, {NULL, NULL}
  // Device state maintained by driver
  void *state;

  // Indicates if device slot is in use
  bool in_use;

  // devfs
  uint32_t id;

  // devfs
  char name[EOS_SMALL_STR_LEN];

  // devfs
  int fd;

  // Indicates whether pins/cfg were dynamically allocated (e.g. via devctl)
  bool dynamic_pins;
  bool dynamic_cfg;

  EOS_TREE_FIELDS(eos_dev_t);
};

// Generates eos_dev_t_tree_{attach,detach,detach_subtree,walk} -
// see emisc/fancytree.h for what each one does. eos_dev_attach/detach
// below wrap these with driver-specific permission checks and
// id/name assignment.
EOS_TREE_DECLARE(eos_dev_t);

// Device registry accessors (dynamic PSRAM vector)
size_t eos_dev_count(void);
eos_dev_t *eos_dev_get(size_t index);

// Initializes device tree
void eos_devtree_init();

// Returns a pointer to root device
eos_dev_t *eos_devtree_root();

// Allocates a new device or returns NULL in case of error
// and sets errno value
eos_dev_t *eos_dev_alloc();

// Attaches device to EOS device tree
int eos_dev_attach(eos_dev_t *dev, eos_dev_t *parent);

// Detaches device and all it's childs from EOS device tree
int eos_dev_detach(eos_dev_t *dev);

// Seek device in a tree
eos_dev_t *eos_dev_find_by_name(const char *name);

// Device tree operations:
int eos_dev_open(eos_dev_t *dev, const char *path, int flags, int mode);
int eos_dev_close(eos_dev_t *dev, int fd);
ssize_t eos_dev_read(eos_dev_t *dev, int fd, void *dst, size_t size);
ssize_t eos_dev_write(eos_dev_t *dev, int fd, const void *data, size_t size);
off_t eos_dev_lseek(eos_dev_t *dev, int fd, off_t offset, int whence);
ssize_t eos_dev_pread(eos_dev_t *dev, int fd, void *dst, size_t size, off_t offset);
ssize_t eos_dev_pwrite(eos_dev_t *dev, int fd, const void *src, size_t size, off_t offset);
int eos_dev_ioctl(eos_dev_t *dev, int fd, int cmd, ...);
int eos_dev_ioctl_call(eos_dev_t *dev, int fd, int cmd, ...);
DIR *eos_dev_opendir(eos_dev_t *dev, const char *path);
struct dirent *eos_dev_readdir(eos_dev_t *dev, DIR *pdir);
long eos_dev_telldir(eos_dev_t *dev, DIR *pdir);
void eos_dev_seekdir(eos_dev_t *dev, DIR *pdir, long offset);
int eos_dev_closedir(eos_dev_t *dev, DIR *pdir);

