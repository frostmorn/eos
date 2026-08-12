#pragma once

// GLOBAL LINKED_LIST STORING ALL MOUNT POINTS AND RELATED DATA

// RESERACH: mtab
typedef eos_mount_t struct eos_mount_t;
struct eos_mount_t{
  const char *devpath;
  const char *path;
  eos_mount_t *next;
};


