#include "emisc/fancymacro.h"
#include "rootfs.h"
#include <dirent.h>
#include <errno.h>
#include <esp_vfs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

// TODO: something should be exposed by esp-idf to access that count
#define EOS_MAX_VFS 10

// TODO: cry on espressif platform github about s_vfs/s_vfs_count exposal or at least normal API for that matter
static char eos_vfs_list[EOS_MAX_VFS][ESP_VFS_PATH_MAX];

// ── Dir state ─────────────────────────────────────────────────

typedef struct {
  uint32_t esp_idf_fs_index; // must be first
  int idx;                   // current position in path list
} rootfs_dir_t;

// ── VFS callbacks ─────────────────────────────────────────────

static int rootfs_open(void *ctx, const char *path, int flags, int mode) {
  // root filesystem has no openable files — only directories
  errno = EISDIR;
  return -1;
}

static int rootfs_close(void *ctx, int fd) {
  errno = EBADF;
  return -1;
}

static ssize_t rootfs_read(void *ctx, int fd, void *buf, size_t len) {
  errno = EBADF;
  return -1;
}

static ssize_t rootfs_write(void *ctx, int fd, const void *buf, size_t len) {
  errno = EROFS;
  return -1;
}

static int rootfs_stat(void *ctx, const char *path, struct stat *st) {
  if (!st) {
    errno = EINVAL;
    return -1;
  }

  // root itself
  if (strcmp(path, "/") == 0 || strcmp(path, "") == 0) goto ok;

  // another vfs   
  for (uint32_t i = 0; i < EOS_MAX_VFS; i++){
     // TODO: trailing slash
    if (strcmp(eos_vfs_list[i], path) == 0) goto ok;
  }
  
  goto fail; // lol, fail^2
  fail:
    errno = ENOENT;
    return -1;

  ok:
    memset(st, 0, sizeof(*st));
    st->st_mode = S_IFDIR | 0555;
    return 0;
}

static DIR *rootfs_opendir(void *ctx, const char *path) {
  if (strcmp(path, "/") != 0 && strcmp(path, "") != 0) {
    errno = EINVAL;
    return NULL;
  }

  rootfs_dir_t *dir = malloc(sizeof(rootfs_dir_t));
  if (!dir) {
    errno = ENOMEM;
    return NULL;
  }

  dir->idx = 0;
  return (DIR *)dir;
}

static struct dirent *rootfs_readdir(void *ctx, DIR *pdir) {
  rootfs_dir_t *dir = (rootfs_dir_t *)pdir;
  
  // static? hmmmmm, probably ok
  static struct dirent s_entry;
 
  for (int i = dir->idx; i < EOS_MAX_VFS; i++){
    const char *vfs = eos_vfs_list[i];

    // It sucks, but can happen on register/unregister
    if (vfs[0] == '\0') continue;

    memset(&s_entry, 0, sizeof(struct dirent));
   
    s_entry.d_type = DT_DIR;
    s_entry.d_ino = (ino_t)i + 1; // whom gonna use that

    strlcpy(s_entry.d_name, vfs+1, sizeof(s_entry.d_name));
    
    dir->idx = i+1;
    return &s_entry;
  } 

  return NULL; // end of list
}

static int rootfs_closedir(void *ctx, DIR *pdir) {
  if (!pdir) {
    errno = EINVAL;
    return -1;
  }
  free(pdir);
  return 0;
}

static void rootfs_seekdir(void *ctx, DIR *pdir, long offset) {
  rootfs_dir_t *dir = (rootfs_dir_t *)pdir;
  dir->idx = (int)offset;
}

static long rootfs_telldir(void *ctx, DIR *pdir) {
  rootfs_dir_t *dir = (rootfs_dir_t *)pdir;
  return (long)dir->idx;
}


void eos_vfs_register_dummy(const char *base_path){
  // find slot
  for (uint32_t i = 0; i < EOS_MAX_VFS; i++){
    if (eos_vfs_list[i][0] == '\0'){
      strlcpy(eos_vfs_list[i], base_path, ESP_VFS_PATH_MAX);
      break;
    }
  }
}

esp_err_t eos_vfs_register(const char* base_path, const esp_vfs_t* vfs, void* ctx){

  esp_err_t err = esp_vfs_register(base_path, vfs, ctx);

  if (err != ESP_OK) return err;

  eos_vfs_register_dummy(base_path);

  return err;
}

void eos_vfs_unregister_dummy(const char *base_path){
  for (uint32_t i = 0; i < EOS_MAX_VFS; i++){
    if (strcmp(eos_vfs_list[i], base_path) == 0){
      memset(eos_vfs_list[i], 0, ESP_VFS_PATH_MAX);
    }
  }
}

esp_err_t eos_vfs_unregister(const char* base_path){
  
  EOS_LOGE("Unregistering %s\n", base_path);
  
  esp_err_t err = esp_vfs_unregister(base_path);

  if (err != ESP_OK) return err;
 
  eos_vfs_unregister_dummy(base_path); 

  return err;
}
// ── Init ──────────────────────────────────────────────────────

void eos_rootfs_init(void) {
  // Zero memory
  for (uint32_t i = 0; i < EOS_MAX_VFS; i++){
    memset(eos_vfs_list[i], 0, ESP_VFS_PATH_MAX);
  }

  static const esp_vfs_t vfs = {
      .flags = ESP_VFS_FLAG_CONTEXT_PTR,
      .open_p = rootfs_open,
      .close_p = rootfs_close,
      .read_p = rootfs_read,
      .write_p = rootfs_write,
      .stat_p = rootfs_stat,
      .opendir_p = rootfs_opendir,
      .readdir_p = rootfs_readdir,
      .closedir_p = rootfs_closedir,
      .seekdir_p = rootfs_seekdir,
      .telldir_p = rootfs_telldir,
  };

  esp_vfs_register("", &vfs, NULL);
  EOS_LOGI("rootfs: mounted");
}
