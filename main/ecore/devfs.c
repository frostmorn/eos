#include "devfs.h"

// FD -> DEV 
typedef struct{
  int                 fd;                // fd from devfs
  int                 drvfd;             // fd from driver
  eos_dev_t*          dev;               // dev associated with fds
}
devfs_fdmap_t; 

// DIR -> DEV
// RESEARCH: Should we create own DIR and maintain DIR <-> DIR association?
typedef struct{
  uint32_t            esp_idf_fs_index;
  eos_dev_t*          dev;
} 
devfs_dir_t;

// DEVFS State
typedef struct{
  kvec_t(devfs_fd_t)  fds;
} 
devfs_state_t;

static devfs_state_t* eos_devfs_state = NULL;

eos_fdmap_t *devfs_fd_to_fdmap(devfs_state_t *dstate, int fd){
  for (size_t i = 0; i < kv_size(dstate->fds); i++){
    if (kv_A(dstate->fds, i).fd == fd) return &(kv_A(dstate->fds, i));
  }
  // not found
  return NULL;
}

ssize_t eos_devfs_write(devfs_state_t *dstate, int fd, void *data, size_t size){
  eos_fdmap_t *fdmap = devfs_fd_to_fdmap(dstate, fd);

  return eos_drv_write(dev, fdmap->drvfd, data, size);
}

off_t eos_devfs_lseek(devfs_state_t *dstate, int fd, off_t offset, int whence){
  eos_fdmap_t *fdmap = devfs_fd_to_fdmap(dstate, fd);

  return eos_drv_lseek(dev, fdmap->drvfd, offset, whjence);
}

ssize_t eos_devfs_read(devfs_state_t *dstate, int fd, void *dst, size_t size){
  eos_fdmap_t *fdmap = devfs_fd_to_fdmap(dstate, fd);

  return eos_drv_read(dev, fdmap->drvfd, dst, size);
}

ssize_t eos_devfs_pread(devfs_state_t *dstate, int fd, void *dst, size_t size, off_t offset){
  eos_fdmap_t *fdmap = devfs_fd_to_fdmap(dstate, fd);

  return eos_drv_pread(dev, fdmap->drvfd, dst, size, offset);
}

ssize_t eos_devfs_pwrite(devfs_state_t *dstate, int fd, void *src, size_t size, off_t offset){
  eos_fdmap_t *fdmap = devfs_fd_to_fdmap(dstate, fd);

  return eos_drv_pwrite(dev, fdmap->drvfd, src, size, offset);
}

int eos_devfs_open(devfs_state_t *dstate, const char *path, int flags, int mode){
  // find dev by path
  // Call eos_drv_open
  // fill fdmap
  // return code
  devfs_fdmap_t fdmap;
}

int eos_devfs_close(devfs_state_t *dstate, int fd){
  eos_fdmap_t *fdmap = devfs_fd_to_fdmap(dstate, fd);
  // TODO: remove corresponding fdmap
  return eos_drv_close(dev, fdmap->drvfd);

}

int eos_devfs_fstat(devfs_state_t *dstate, int fd, struct stat *st){
  eos_fdmap_t *fdmap = devfs_fd_to_fdmap(dstate, fd);

  return eos_drv_write(dev, fdmap->drvfd, data, size);

}

int eos_devfs_stat(devfs_state_t *dstate, const char *path, struct stat *st){

}

int eos_devfs_link(devfs_state_t *dstate, const char *n1, const char *n2){
// idk how it can work
}

int eos_devfs_unlink(devfs_state_t *dstate, const char *path){

}

int eos_devfs_rename(devfs_state_t *dstate, const char *src, const char *dst){

}

DIR *eos_devfs_opendir(devfs_state_t *dstate, const char *path){

}

struct dirent *eos_devfs_readdir(devfs_state_t *dstate, DIR *pdir){

}

long eos_devfs_telldir(devfs_state_t *dstate, DIR *pdir){

}

void eos_devfs_seekdir(devfs_state_t *dstate, DIR *pdir, long offset){

}

int eos_devfs_closedir(devfs_state_t *dstate, DIR *pdir){

}

int eos_devfs_mkdir(devfs_state_t *dstate, const char *name, mode_t mode){

}

int eos_devfs_rmdir(devfs_state_t *dstate, const char *name){

}

int eos_devfs_fcntl(devfs_state_t *dstate, int fd, int cmd, int arg){
  eos_fdmap_t *fdmap = devfs_fd_to_fdmap(dstate, fd);

  return eos_drv_write(dev, fdmap->drvfd, data, size);

}

int eos_devfs_ioctl(devfs_state_t *dstate, int fd, int cmd, va_list args){
  eos_fdmap_t *fdmap = devfs_fd_to_fdmap(dstate, fd);

  return eos_drv_write(dev, fdmap->drvfd, data, size);

}

int eos_devfs_fsync(devfs_state_t *dstate, int fd){
  eos_fdmap_t *fdmap = devfs_fd_to_fdmap(dstate, fd);

  return eos_drv_write(dev, fdmap->drvfd, data, size);

}

int eos_devfs_access(devfs_state_t *dstate, const char *path, int amode){

}

int eos_devfs_truncate(devfs_state_t *dstate, const char *path, off_t length){

}

int eos_devfs_ftruncate(devfs_state_t *dstate, int fd, off_t length){
  eos_fdmap_t *fdmap = devfs_fd_to_fdmap(dstate, fd);

  return eos_drv_write(dev, fdmap->drvfd, data, size);

}

int eos_devfs_utime(devfs_state_t *dstate, const char *path, const struct utimbuf *times){

}

devfs_state_t *eos_devfs_mount(const char *path){
  // Allocate devfs state
  devfs_state_t * state = malloc(sizeof(devfs_state_t));
  memset(state, 0, sizeof(devfs_state_t));
  kv_init(state->fds);

}

void eos_devfs_init(){
  eos_devfs_state = eos_devfs_mount(EOS_DEVFS_ROOT);
}
