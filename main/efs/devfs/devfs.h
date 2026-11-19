#pragma once
// Requires tmpfs to be mounted

// In theory we had to use tmpfs here, and just provide dir/file for each dev
// but idf is somehow limited in terms of vfs count, which isn't compatible
// with idea to expose everything as VFS

// Therefore we require that layer -_-

#ifndef EOS_DEVFS_ROOT
#define EOS_DEVFS_ROOT "/dev"
#endif

void eos_devfs_init();
