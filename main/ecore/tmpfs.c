#include "tmpfs.h"

typedef struct{
  char                   data  [TMP_SECTOR_SIZE];
}tmpfs_fblock_t;

// TODO: Store stat + dirent directly on a file node?
typedef struct tmpfs_fnode_t tmpfs_fnode_t;
struct tmpfs_fnode_t{
  char                   name                 [TMP_NODE_NAME_MAX];
  uint32_t               nhash;
  mode_t                 mode;
  time_t                 atime, mtime, ctime;
  int32_t                refcount; // -1 means file marked for deletion

  kvec_t(tmpfs_fblock_t) blocks;
};

typedef struct{
  uint32_t esp_idf_fs_index;
  int idx;
} tmpfs_dir_t; 

typedef struct{
  kvec_t(tmpfs_fnode_t)    entries;
  kvec_t(int)              fds;
}tmpfs_state_t;

ssize_t tmpfs_write(tmpfs_state_t *ctx, int fd, void *data, size_t size){

}

off_t tmpfs_lseek(tmpfs_state_t *ctx, int fd, off_t offset, int whence){

}

ssize_t tmpfs_read(tmpfs_state_t *ctx, int fd, void *dst, size_t size){

}

int tmpfs_open(tmpfs_state_t *ctx, const char *path, int flags, int mode){

}

int tmpfs_close(tmpfs_state_t *ctx, int fd){

}

int tmpfs_stat(tmpfs_state_t *ctx, const char *path, struct stat *st){

}

int tmpfs_unlink(tmpfs_state_t *ctx, const char *path){

}

int tmpfs_rename(tmpfs_state_t *ctx, const char *src, const char *dst){

}

DIR* tmpfs_opendir(tmpfs_state_t *ctx, const char *path){

}

struct dirent *tmpfs_readdir(tmpfs_state_t *ctx, DIR *pdir){

}

long tmpfs_telldir(tmpfs_state_t *ctx, DIR *pdir){

}

void tmpfs_seekdir(tmpfs_state_t *ctx, DIR *pdir, long offset){

}

int tmpfs_closedir(tmpfs_state_t *ctx, DIR *pdir){

}

int tmpfs_mkdir(tmpfs_state_t *ctx, const char *name, mode_t mode){

}

int tmpfs_rmdir(tmpfs_state_t *ctx, const char *name){

}

int tmpfs_access(tmpfs_state_t *ctx, const char *path, int amode){

}

int tmpfs_utime(tmpfs_state_t *ctx, const char *path, const struct utimbuf *times){

}

void eos_tmpfs_mount(const char *path){


}
