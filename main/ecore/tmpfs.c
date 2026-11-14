#include "tmpfs.h"
#include "ecore/proc.h"
#include "ecore/vfs.h"
#include "emisc/fancymacro.h"
#include "emisc/fancytree.h"
#include "emisc/fnv-1a-32.h"
#include "emisc/kvec.h"
#include "emisc/psram_alloc.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <utime.h>

///////////////////////////////////////////////////////////////////
// Node & Inode Structures
///////////////////////////////////////////////////////////////////

typedef struct {
  char data[TMP_SECTOR_SIZE];
} tmpfs_fblock_t;

typedef struct {
  mode_t mode;
  time_t atime, mtime, ctime;
  size_t fsize;
  uint16_t nlink;
  int32_t refcount;
  char *symlink_target;
  kvec_psram_t(tmpfs_fblock_t) blocks;
} tmpfs_inode_t;

typedef struct tmpfs_fnode_t tmpfs_fnode_t;
struct tmpfs_fnode_t {
  EOS_TREE_FIELDS(tmpfs_fnode_t)

  char name[TMP_NODE_NAME_MAX];
  uint32_t nhash;
  tmpfs_inode_t *inode;
};

EOS_TREE_DECLARE(tmpfs_fnode_t);
EOS_TREE_DEFINE(tmpfs_fnode_t)

typedef struct {
  eos_vfs_file_t base;
  tmpfs_inode_t *inode;
  long offset;
} tmpfs_file_t;

typedef struct {
  eos_vfs_dir_t base;
  tmpfs_fnode_t *dir;
  tmpfs_fnode_t *cur;
  struct dirent entry;
} tmpfs_dir_t;

typedef struct {
  tmpfs_fnode_t *root;
  pthread_mutex_t lock;
} tmpfs_state_t;

static inline void tmpfs_lock(tmpfs_state_t *state) {
  if (state) pthread_mutex_lock(&state->lock);
}

static inline void tmpfs_unlock(tmpfs_state_t *state) {
  if (state) pthread_mutex_unlock(&state->lock);
}

static tmpfs_state_t *eos_tmpfs_state = NULL;

///////////////////////////////////////////////////////////////////
// Inode Lifecycle
///////////////////////////////////////////////////////////////////

static tmpfs_inode_t *tmpfs_create_inode(mode_t mode) {
  bool prev_k = eos_proc_enter_kernel();
  tmpfs_inode_t *ino = eos_psram_calloc_prefer(1, sizeof(tmpfs_inode_t));
  if (!ino) {
    eos_proc_leave_kernel(prev_k);
    return NULL;
  }
  ino->mode = mode;
  ino->atime = ino->mtime = ino->ctime = time(NULL);
  ino->nlink = 1;
  ino->refcount = 0;
  kv_init(ino->blocks);
  eos_proc_leave_kernel(prev_k);
  return ino;
}

static void tmpfs_free_inode(tmpfs_inode_t *ino) {
  if (!ino) return;
  bool prev_k = eos_proc_enter_kernel();
  kv_destroy(ino->blocks);
  if (ino->symlink_target) {
    free(ino->symlink_target);
    ino->symlink_target = NULL;
  }
  free(ino);
  eos_proc_leave_kernel(prev_k);
}

///////////////////////////////////////////////////////////////////
// Name matching & Path Resolution
///////////////////////////////////////////////////////////////////

static bool tmpfs_match_name(tmpfs_fnode_t *node, void *ctx) {
  return strcmp(node->name, (const char *)ctx) == 0;
}

static tmpfs_fnode_t *tmpfs_find_node(tmpfs_fnode_t *root, const char *path) {
  if (!path)
    return NULL;
  if (path[0] == '\0' || (path[0] == '/' && path[1] == '\0'))
    return root;

  tmpfs_fnode_t *cur = root;
  const char *p = path;

  while (*p && cur) {
    while (*p == '/')
      p++;
    if (!*p)
      break;

    const char *seg_start = p;
    while (*p && *p != '/')
      p++;
    size_t seg_len = (size_t)(p - seg_start);

    if (seg_len == 1 && seg_start[0] == '.')
      continue;

    if (seg_len == 2 && seg_start[0] == '.' && seg_start[1] == '.') {
      if (cur->parent)
        cur = cur->parent;
      continue;
    }

    if (seg_len >= TMP_NODE_NAME_MAX)
      return NULL;

    char seg[TMP_NODE_NAME_MAX];
    memcpy(seg, seg_start, seg_len);
    seg[seg_len] = '\0';

    cur = tmpfs_fnode_t_tree_find_child(cur, tmpfs_match_name, seg);
  }

  return cur;
}

static tmpfs_fnode_t *tmpfs_resolve_parent(tmpfs_fnode_t *root,
                                           const char *path, char *namebuf,
                                           size_t namebuf_size) {
  const char *lastSlash = strrchr(path, '/');
  const char *basename = lastSlash ? lastSlash + 1 : path;

  if (basename[0] == '\0') {
    errno = ENOENT;
    return NULL;
  }
  if (strlen(basename) >= namebuf_size) {
    errno = ENAMETOOLONG;
    return NULL;
  }
  strcpy(namebuf, basename);

  if (!lastSlash)
    return root;

  size_t dirlen = (size_t)(lastSlash - path);
  if (dirlen == 0)
    return root;

  char dirbuf[EOS_MID_STR_LEN];
  if (dirlen >= sizeof(dirbuf)) {
    errno = ENAMETOOLONG;
    return NULL;
  }
  memcpy(dirbuf, path, dirlen);
  dirbuf[dirlen] = '\0';

  tmpfs_fnode_t *parent = tmpfs_find_node(root, dirbuf);
  if (!parent) {
    errno = ENOENT;
    return NULL;
  }
  return parent;
}

///////////////////////////////////////////////////////////////////
// Node Lifecycle
///////////////////////////////////////////////////////////////////

static tmpfs_fnode_t *tmpfs_create_node(const char *name, mode_t mode,
                                        tmpfs_fnode_t *parent) {
  if (!parent || !name || name[0] == '\0') {
    errno = EINVAL;
    return NULL;
  }
  if (!parent->inode || !S_ISDIR(parent->inode->mode)) {
    errno = ENOTDIR;
    return NULL;
  }
  if (strlen(name) >= TMP_NODE_NAME_MAX) {
    errno = ENAMETOOLONG;
    return NULL;
  }
  if (tmpfs_fnode_t_tree_find_child(parent, tmpfs_match_name, (void *)name)) {
    errno = EEXIST;
    return NULL;
  }

  tmpfs_inode_t *ino = tmpfs_create_inode(mode);
  if (!ino) {
    errno = ENOMEM;
    return NULL;
  }

  bool prev_k = eos_proc_enter_kernel();
  tmpfs_fnode_t *node = eos_psram_calloc_prefer(1, sizeof(tmpfs_fnode_t));
  if (!node) {
    tmpfs_free_inode(ino);
    eos_proc_leave_kernel(prev_k);
    errno = ENOMEM;
    return NULL;
  }

  strlcpy(node->name, name, sizeof(node->name));
  node->nhash = fnv_32a_cstr(name, parent->nhash);
  node->inode = ino;

  tmpfs_fnode_t_tree_attach(node, parent);
  eos_proc_leave_kernel(prev_k);

  return node;
}

static void tmpfs_delete_node(tmpfs_fnode_t *node) {
  if (!node)
    return;
  bool prev_k = eos_proc_enter_kernel();
  tmpfs_fnode_t_tree_detach(node);
  if (node->inode) {
    if (node->inode->nlink > 0)
      node->inode->nlink--;
    if (node->inode->nlink == 0 && node->inode->refcount == 0) {
      tmpfs_free_inode(node->inode);
      node->inode = NULL;
    }
  }
  free(node);
  eos_proc_leave_kernel(prev_k);
}

static void tmpfs_fill_stat(const tmpfs_inode_t *ino, struct stat *st) {
  memset(st, 0, sizeof(*st));
  st->st_mode = ino->mode;
  st->st_size = (off_t)ino->fsize;
  st->st_atime = ino->atime;
  st->st_mtime = ino->mtime;
  st->st_ctime = ino->ctime;
  st->st_nlink = ino->nlink;
  st->st_blksize = TMP_SECTOR_SIZE;
  st->st_blocks = (blkcnt_t)kv_size(ino->blocks);
}

///////////////////////////////////////////////////////////////////
// VFS Operations
///////////////////////////////////////////////////////////////////

static ssize_t tmpfs_write(void *ctx, eos_vfs_file_t *file, const void *src, size_t size) {
  tmpfs_state_t *state = (tmpfs_state_t *)ctx;
  if (!state || !file || !src) {
    errno = EINVAL;
    return -1;
  }
  if (!size)
    return 0;

  tmpfs_lock(state);

  tmpfs_file_t *tf = (tmpfs_file_t *)file;
  tmpfs_inode_t *inode = tf->inode;
  if (!inode) {
    tmpfs_unlock(state);
    errno = EBADF;
    return -1;
  }

  if (S_ISDIR(inode->mode)) {
    tmpfs_unlock(state);
    errno = EISDIR;
    return -1;
  }

  int acc = file->flags & O_ACCMODE;
  if (acc != O_WRONLY && acc != O_RDWR) {
    tmpfs_unlock(state);
    errno = EBADF;
    return -1;
  }

  if (file->flags & O_APPEND)
    tf->offset = (long)inode->fsize;

  size_t writeEnd = (size_t)tf->offset + size;
  size_t blocksNeeded = (writeEnd + TMP_SECTOR_SIZE - 1) / TMP_SECTOR_SIZE;

  bool prev_k = eos_proc_enter_kernel();
  while (kv_size(inode->blocks) < blocksNeeded) {
    tmpfs_fblock_t blk;
    memset(&blk, 0, sizeof(blk));
    kv_push(tmpfs_fblock_t, inode->blocks, blk);
  }
  eos_proc_leave_kernel(prev_k);

  size_t remaining = size, srcOff = 0, pos = (size_t)tf->offset;
  const uint8_t *data = (const uint8_t *)src;

  while (remaining > 0) {
    size_t blockIdx = pos / TMP_SECTOR_SIZE;
    size_t byteIdx = pos % TMP_SECTOR_SIZE;
    size_t chunk = TMP_SECTOR_SIZE - byteIdx;
    if (chunk > remaining)
      chunk = remaining;

    memcpy(&kv_A(inode->blocks, blockIdx).data[byteIdx], data + srcOff, chunk);

    pos += chunk;
    srcOff += chunk;
    remaining -= chunk;
  }

  inode->atime = inode->mtime = time(NULL);
  if (writeEnd > inode->fsize)
    inode->fsize = writeEnd;
  tf->offset = (long)pos;

  tmpfs_unlock(state);
  return (ssize_t)size;
}

static ssize_t tmpfs_read(void *ctx, eos_vfs_file_t *file, void *dst, size_t size) {
  tmpfs_state_t *state = (tmpfs_state_t *)ctx;
  if (!state || !file || !dst) {
    errno = EINVAL;
    return -1;
  }
  if (!size)
    return 0;

  tmpfs_lock(state);

  tmpfs_file_t *tf = (tmpfs_file_t *)file;
  tmpfs_inode_t *inode = tf->inode;
  if (!inode) {
    tmpfs_unlock(state);
    errno = EBADF;
    return -1;
  }

  if (S_ISDIR(inode->mode)) {
    tmpfs_unlock(state);
    errno = EISDIR;
    return -1;
  }

  if ((file->flags & O_ACCMODE) == O_WRONLY) {
    tmpfs_unlock(state);
    errno = EBADF;
    return -1;
  }

  if ((size_t)tf->offset >= inode->fsize) {
    tmpfs_unlock(state);
    return 0;
  }

  size_t available = inode->fsize - (size_t)tf->offset;
  size_t toRead = (size < available) ? size : available;

  size_t remaining = toRead, dstOff = 0, pos = (size_t)tf->offset;
  uint8_t *out = (uint8_t *)dst;

  while (remaining > 0) {
    size_t blockIdx = pos / TMP_SECTOR_SIZE;
    size_t byteIdx = pos % TMP_SECTOR_SIZE;
    size_t chunk = TMP_SECTOR_SIZE - byteIdx;
    if (chunk > remaining)
      chunk = remaining;

    memcpy(out + dstOff, &kv_A(inode->blocks, blockIdx).data[byteIdx], chunk);

    pos += chunk;
    dstOff += chunk;
    remaining -= chunk;
  }

  tf->offset += (long)toRead;
  inode->atime = time(NULL);

  tmpfs_unlock(state);
  return (ssize_t)toRead;
}

static off_t tmpfs_lseek(void *ctx, eos_vfs_file_t *file, off_t offset, int whence) {
  tmpfs_state_t *state = (tmpfs_state_t *)ctx;
  if (!state || !file) {
    errno = EBADF;
    return -1;
  }
  tmpfs_lock(state);

  tmpfs_file_t *tf = (tmpfs_file_t *)file;
  tmpfs_inode_t *inode = tf->inode;
  if (!inode) {
    tmpfs_unlock(state);
    errno = EBADF;
    return -1;
  }

  long newOffset;
  switch (whence) {
  case SEEK_SET:
    newOffset = (long)offset;
    break;
  case SEEK_CUR:
    newOffset = tf->offset + (long)offset;
    break;
  case SEEK_END:
    newOffset = (long)inode->fsize + (long)offset;
    break;
  default:
    tmpfs_unlock(state);
    errno = EINVAL;
    return -1;
  }

  if (newOffset < 0) {
    tmpfs_unlock(state);
    errno = EINVAL;
    return -1;
  }

  tf->offset = newOffset;
  tmpfs_unlock(state);
  return (off_t)newOffset;
}

static int tmpfs_open(void *ctx, const char *path, int flags, int mode, eos_vfs_file_t **out_file) {
  (void)mode;
  tmpfs_state_t *state = (tmpfs_state_t *)ctx;

  if (!state || !path) {
    errno = EINVAL;
    return -1;
  }
  tmpfs_lock(state);

  tmpfs_fnode_t *node = tmpfs_find_node(state->root, path);

  if (node && (flags & O_CREAT) && (flags & O_EXCL)) {
    tmpfs_unlock(state);
    errno = EEXIST;
    return -1;
  }

  if (!node) {
    if (!(flags & O_CREAT)) {
      tmpfs_unlock(state);
      errno = ENOENT;
      return -1;
    }

    char name[TMP_NODE_NAME_MAX];
    tmpfs_fnode_t *parent =
        tmpfs_resolve_parent(state->root, path, name, sizeof(name));
    if (!parent) {
      tmpfs_unlock(state);
      return -1;
    }

    node = tmpfs_create_node(name, TMP_FILE_MODE, parent);
    if (!node) {
      tmpfs_unlock(state);
      return -1;
    }
  }

  if (node->inode && S_ISDIR(node->inode->mode)) {
    tmpfs_unlock(state);
    errno = EISDIR;
    return -1;
  }

  if (flags & O_TRUNC) {
    if (node->inode) {
      bool prev_k = eos_proc_enter_kernel();
      kv_destroy(node->inode->blocks);
      kv_init(node->inode->blocks);
      eos_proc_leave_kernel(prev_k);
      node->inode->fsize = 0;
      node->inode->atime = node->inode->ctime = time(NULL);
    }
  }

  tmpfs_file_t *file = eos_vfs_file_alloc(NULL, sizeof(tmpfs_file_t), flags);
  if (!file) {
    tmpfs_unlock(state);
    errno = ENOMEM;
    return -1;
  }

  file->inode = node->inode;
  if (file->inode) {
    file->inode->refcount++;
  }
  file->offset = (flags & O_APPEND) ? (long)node->inode->fsize : 0;

  *out_file = &file->base;
  int fd = file->base.fd;

  tmpfs_unlock(state);
  return fd;
}

static int tmpfs_close(void *ctx, eos_vfs_file_t *file) {
  tmpfs_state_t *state = (tmpfs_state_t *)ctx;
  if (!state || !file)
    return 0;
  tmpfs_lock(state);

  tmpfs_file_t *tf = (tmpfs_file_t *)file;
  tmpfs_inode_t *ino = tf->inode;
  if (ino) {
    if (ino->refcount > 0)
      ino->refcount--;
    if (ino->nlink == 0 && ino->refcount == 0) {
      tmpfs_free_inode(ino);
      tf->inode = NULL;
    }
  }

  tmpfs_unlock(state);
  return 0;
}

static int tmpfs_stat(void *ctx, const char *path, struct stat *st) {
  tmpfs_state_t *state = (tmpfs_state_t *)ctx;

  if (!state || !path || !st) {
    errno = EINVAL;
    return -1;
  }
  tmpfs_lock(state);

  tmpfs_fnode_t *node = tmpfs_find_node(state->root, path);
  if (!node || !node->inode) {
    tmpfs_unlock(state);
    errno = ENOENT;
    return -1;
  }

  tmpfs_fill_stat(node->inode, st);
  tmpfs_unlock(state);
  return 0;
}

static int tmpfs_fstat(void *ctx, eos_vfs_file_t *file, struct stat *st) {
  tmpfs_state_t *state = (tmpfs_state_t *)ctx;
  if (!state || !file || !st) {
    errno = EINVAL;
    return -1;
  }
  tmpfs_lock(state);

  tmpfs_file_t *tf = (tmpfs_file_t *)file;
  if (!tf->inode) {
    tmpfs_unlock(state);
    errno = EBADF;
    return -1;
  }

  tmpfs_fill_stat(tf->inode, st);
  tmpfs_unlock(state);
  return 0;
}

static int tmpfs_unlink(void *ctx, const char *path) {
  tmpfs_state_t *state = (tmpfs_state_t *)ctx;

  if (!state || !path) {
    errno = EINVAL;
    return -1;
  }
  tmpfs_lock(state);

  tmpfs_fnode_t *node = tmpfs_find_node(state->root, path);
  if (!node) {
    tmpfs_unlock(state);
    errno = ENOENT;
    return -1;
  }
  if (node == state->root) {
    tmpfs_unlock(state);
    errno = EBUSY;
    return -1;
  }
  if (node->inode && S_ISDIR(node->inode->mode) && node->child) {
    tmpfs_unlock(state);
    errno = ENOTEMPTY;
    return -1;
  }

  tmpfs_delete_node(node);

  tmpfs_unlock(state);
  return 0;
}

static int tmpfs_rename(void *ctx, const char *src, const char *dst) {
  tmpfs_state_t *state = (tmpfs_state_t *)ctx;

  if (!state || !src || !dst) {
    errno = EINVAL;
    return -1;
  }
  tmpfs_lock(state);

  tmpfs_fnode_t *node = tmpfs_find_node(state->root, src);
  if (!node || !node->inode) {
    tmpfs_unlock(state);
    errno = ENOENT;
    return -1;
  }
  if (node == state->root) {
    tmpfs_unlock(state);
    errno = EBUSY;
    return -1;
  }

  char name[TMP_NODE_NAME_MAX];
  tmpfs_fnode_t *dstParent =
      tmpfs_resolve_parent(state->root, dst, name, sizeof(name));
  if (!dstParent) {
    tmpfs_unlock(state);
    return -1;
  }

  tmpfs_fnode_t *dstNode =
      tmpfs_fnode_t_tree_find_child(dstParent, tmpfs_match_name, name);
  if (dstNode) {
    if (dstNode->inode && S_ISDIR(dstNode->inode->mode) && dstNode->child) {
      tmpfs_unlock(state);
      errno = ENOTEMPTY;
      return -1;
    }
    if (S_ISDIR(node->inode->mode) != (dstNode->inode && S_ISDIR(dstNode->inode->mode))) {
      tmpfs_unlock(state);
      errno = S_ISDIR(node->inode->mode) ? ENOTDIR : EISDIR;
      return -1;
    }
    tmpfs_delete_node(dstNode);
  }

  // Unlink node from current parent without reparenting its children
  if (node->parent) {
    tmpfs_fnode_t *parent = node->parent;
    if (parent->child == node) {
      parent->child = node->next;
    } else {
      tmpfs_fnode_t *cur = parent->child;
      while (cur && cur->next != node) cur = cur->next;
      if (cur && cur->next == node) cur->next = node->next;
    }
    node->parent = NULL;
    node->next = NULL;
  }

  strlcpy(node->name, name, sizeof(node->name));
  node->nhash = fnv_32a_cstr(name, dstParent->nhash);
  node->inode->mtime = node->inode->ctime = time(NULL);
  tmpfs_fnode_t_tree_attach(node, dstParent);

  tmpfs_unlock(state);
  return 0;
}

static int tmpfs_link(void *ctx, const char *oldpath, const char *newpath) {
  tmpfs_state_t *state = (tmpfs_state_t *)ctx;
  if (!state || !oldpath || !newpath) {
    errno = EINVAL;
    return -1;
  }
  tmpfs_lock(state);

  tmpfs_fnode_t *oldnode = tmpfs_find_node(state->root, oldpath);
  if (!oldnode || !oldnode->inode) {
    tmpfs_unlock(state);
    errno = ENOENT;
    return -1;
  }

  if (S_ISDIR(oldnode->inode->mode)) {
    tmpfs_unlock(state);
    errno = EPERM;
    return -1;
  }

  char name[TMP_NODE_NAME_MAX];
  tmpfs_fnode_t *newparent =
      tmpfs_resolve_parent(state->root, newpath, name, sizeof(name));
  if (!newparent) {
    tmpfs_unlock(state);
    return -1;
  }

  if (tmpfs_fnode_t_tree_find_child(newparent, tmpfs_match_name, (void *)name)) {
    tmpfs_unlock(state);
    errno = EEXIST;
    return -1;
  }

  bool prev_k = eos_proc_enter_kernel();
  tmpfs_fnode_t *newnode = eos_psram_calloc_prefer(1, sizeof(tmpfs_fnode_t));
  if (!newnode) {
    eos_proc_leave_kernel(prev_k);
    tmpfs_unlock(state);
    errno = ENOMEM;
    return -1;
  }

  strlcpy(newnode->name, name, sizeof(newnode->name));
  newnode->nhash = fnv_32a_cstr(name, newparent->nhash);
  newnode->inode = oldnode->inode;
  newnode->inode->nlink++;
  newnode->inode->ctime = time(NULL);

  tmpfs_fnode_t_tree_attach(newnode, newparent);
  eos_proc_leave_kernel(prev_k);

  tmpfs_unlock(state);
  return 0;
}

static int tmpfs_symlink(void *ctx, const char *target, const char *linkpath) {
  tmpfs_state_t *state = (tmpfs_state_t *)ctx;
  if (!state || !target || !linkpath) {
    errno = EINVAL;
    return -1;
  }
  tmpfs_lock(state);

  char name[TMP_NODE_NAME_MAX];
  tmpfs_fnode_t *parent =
      tmpfs_resolve_parent(state->root, linkpath, name, sizeof(name));
  if (!parent) {
    tmpfs_unlock(state);
    return -1;
  }

  if (tmpfs_fnode_t_tree_find_child(parent, tmpfs_match_name, (void *)name)) {
    tmpfs_unlock(state);
    errno = EEXIST;
    return -1;
  }

  tmpfs_inode_t *ino = tmpfs_create_inode(S_IFLNK | 0777);
  if (!ino) {
    tmpfs_unlock(state);
    errno = ENOMEM;
    return -1;
  }

  ino->symlink_target = strdup(target);
  if (!ino->symlink_target) {
    tmpfs_free_inode(ino);
    tmpfs_unlock(state);
    errno = ENOMEM;
    return -1;
  }
  ino->fsize = strlen(target);

  bool prev_k = eos_proc_enter_kernel();
  tmpfs_fnode_t *node = eos_psram_calloc_prefer(1, sizeof(tmpfs_fnode_t));
  if (!node) {
    tmpfs_free_inode(ino);
    eos_proc_leave_kernel(prev_k);
    tmpfs_unlock(state);
    errno = ENOMEM;
    return -1;
  }

  strlcpy(node->name, name, sizeof(node->name));
  node->nhash = fnv_32a_cstr(name, parent->nhash);
  node->inode = ino;

  tmpfs_fnode_t_tree_attach(node, parent);
  eos_proc_leave_kernel(prev_k);

  tmpfs_unlock(state);
  return 0;
}

static ssize_t tmpfs_readlink(void *ctx, const char *path, char *buf, size_t bufsiz) {
  tmpfs_state_t *state = (tmpfs_state_t *)ctx;
  if (!state || !path || !buf || bufsiz == 0) {
    errno = EINVAL;
    return -1;
  }
  tmpfs_lock(state);

  tmpfs_fnode_t *node = tmpfs_find_node(state->root, path);
  if (!node || !node->inode) {
    tmpfs_unlock(state);
    errno = ENOENT;
    return -1;
  }

  if (!S_ISLNK(node->inode->mode)) {
    tmpfs_unlock(state);
    errno = EINVAL;
    return -1;
  }

  if (!node->inode->symlink_target) {
    tmpfs_unlock(state);
    errno = EIO;
    return -1;
  }

  size_t len = strlen(node->inode->symlink_target);
  size_t to_copy = (len < bufsiz) ? len : bufsiz;
  memcpy(buf, node->inode->symlink_target, to_copy);

  tmpfs_unlock(state);
  return (ssize_t)to_copy;
}

static int tmpfs_lstat(void *ctx, const char *path, struct stat *st) {
  tmpfs_state_t *state = (tmpfs_state_t *)ctx;
  if (!state || !path || !st) {
    errno = EINVAL;
    return -1;
  }
  tmpfs_lock(state);

  tmpfs_fnode_t *node = tmpfs_find_node(state->root, path);
  if (!node || !node->inode) {
    tmpfs_unlock(state);
    errno = ENOENT;
    return -1;
  }

  tmpfs_fill_stat(node->inode, st);
  tmpfs_unlock(state);
  return 0;
}

static int tmpfs_opendir(void *ctx, const char *path, eos_vfs_dir_t **out_dir) {
  tmpfs_state_t *state = (tmpfs_state_t *)ctx;
  if (!state || !path) {
    errno = EINVAL;
    return -1;
  }
  tmpfs_lock(state);

  tmpfs_fnode_t *node = tmpfs_find_node(state->root, path);
  if (!node || !node->inode) {
    tmpfs_unlock(state);
    errno = ENOENT;
    return -1;
  }
  if (!S_ISDIR(node->inode->mode)) {
    tmpfs_unlock(state);
    errno = ENOTDIR;
    return -1;
  }

  tmpfs_dir_t *dir = eos_vfs_dir_alloc(NULL, sizeof(tmpfs_dir_t));
  if (!dir) {
    tmpfs_unlock(state);
    errno = ENOMEM;
    return -1;
  }

  dir->dir = node;
  dir->cur = node->child;
  node->inode->refcount++;

  *out_dir = &dir->base;
  tmpfs_unlock(state);
  return 0;
}

static struct dirent *tmpfs_readdir(void *ctx, eos_vfs_dir_t *pdir) {
  tmpfs_state_t *state = (tmpfs_state_t *)ctx;
  if (!state || !pdir) {
    errno = EINVAL;
    return NULL;
  }
  tmpfs_lock(state);

  tmpfs_dir_t *dir = (tmpfs_dir_t *)pdir;
  tmpfs_fnode_t *node = dir->cur;
  if (!node) {
    tmpfs_unlock(state);
    return NULL;
  }

  dir->cur = node->next;

  memset(&dir->entry, 0, sizeof(dir->entry));
  dir->entry.d_ino = (ino_t)(uintptr_t)node;
  if (node->inode) {
    if (S_ISDIR(node->inode->mode)) {
      dir->entry.d_type = DT_DIR;
    } else if (S_ISLNK(node->inode->mode)) {
      dir->entry.d_type = DT_LNK;
    } else {
      dir->entry.d_type = DT_REG;
    }
  } else {
    dir->entry.d_type = DT_REG;
  }
  strlcpy(dir->entry.d_name, node->name, sizeof(dir->entry.d_name));

  tmpfs_unlock(state);
  return &dir->entry;
}

static int tmpfs_closedir(void *ctx, eos_vfs_dir_t *pdir) {
  tmpfs_state_t *state = (tmpfs_state_t *)ctx;
  if (!state || !pdir)
    return 0;
  tmpfs_lock(state);

  tmpfs_dir_t *dir = (tmpfs_dir_t *)pdir;
  tmpfs_fnode_t *node = dir->dir;
  if (node && node->inode) {
    if (node->inode->refcount > 0)
      node->inode->refcount--;
    if (node->inode->nlink == 0 && node->inode->refcount == 0) {
      tmpfs_free_inode(node->inode);
      node->inode = NULL;
    }
  }

  tmpfs_unlock(state);
  return 0;
}

static void tmpfs_seekdir(void *ctx, eos_vfs_dir_t *pdir, long loc) {
  tmpfs_state_t *state = (tmpfs_state_t *)ctx;
  if (!state || !pdir) return;
  tmpfs_lock(state);
  tmpfs_dir_t *dir = (tmpfs_dir_t *)pdir;
  if (loc == 0) {
    dir->cur = dir->dir ? dir->dir->child : NULL;
  } else {
    tmpfs_fnode_t *target = (tmpfs_fnode_t *)(uintptr_t)loc;
    dir->cur = target ? target : (dir->dir ? dir->dir->child : NULL);
  }
  tmpfs_unlock(state);
}

static long tmpfs_telldir(void *ctx, eos_vfs_dir_t *pdir) {
  tmpfs_state_t *state = (tmpfs_state_t *)ctx;
  if (!state || !pdir) return -1;
  tmpfs_lock(state);
  tmpfs_dir_t *dir = (tmpfs_dir_t *)pdir;
  long ret = (long)(intptr_t)dir->cur;
  tmpfs_unlock(state);
  return ret;
}

static int tmpfs_mkdir(void *ctx, const char *path, mode_t mode) {
  (void)mode;
  tmpfs_state_t *state = (tmpfs_state_t *)ctx;
  if (!state || !path) {
    errno = EINVAL;
    return -1;
  }
  tmpfs_lock(state);

  char name[TMP_NODE_NAME_MAX];
  tmpfs_fnode_t *parent =
      tmpfs_resolve_parent(state->root, path, name, sizeof(name));
  if (!parent) {
    tmpfs_unlock(state);
    return -1;
  }

  tmpfs_fnode_t *node = tmpfs_create_node(name, TMP_DIR_MODE, parent);
  if (!node) {
    tmpfs_unlock(state);
    return -1;
  }

  tmpfs_unlock(state);
  return 0;
}

static int tmpfs_rmdir(void *ctx, const char *path) {
  tmpfs_state_t *state = (tmpfs_state_t *)ctx;
  if (!state || !path) {
    errno = EINVAL;
    return -1;
  }
  tmpfs_lock(state);

  tmpfs_fnode_t *node = tmpfs_find_node(state->root, path);
  if (!node || !node->inode) {
    tmpfs_unlock(state);
    errno = ENOENT;
    return -1;
  }
  if (!S_ISDIR(node->inode->mode)) {
    tmpfs_unlock(state);
    errno = ENOTDIR;
    return -1;
  }
  if (node == state->root) {
    tmpfs_unlock(state);
    errno = EBUSY;
    return -1;
  }
  if (node->child) {
    tmpfs_unlock(state);
    errno = ENOTEMPTY;
    return -1;
  }
  if (node->inode->refcount > 0) {
    tmpfs_unlock(state);
    errno = EBUSY;
    return -1;
  }

  tmpfs_delete_node(node);
  tmpfs_unlock(state);
  return 0;
}

static int tmpfs_access(void *ctx, const char *path, int amode) {
  tmpfs_state_t *state = (tmpfs_state_t *)ctx;
  if (!state || !path) {
    errno = EINVAL;
    return -1;
  }
  tmpfs_lock(state);

  tmpfs_fnode_t *node = tmpfs_find_node(state->root, path);
  if (!node || !node->inode) {
    tmpfs_unlock(state);
    errno = ENOENT;
    return -1;
  }

  if (amode == F_OK) {
    tmpfs_unlock(state);
    return 0;
  }

  mode_t m = node->inode->mode;
  if ((amode & R_OK) && !(m & S_IRUSR)) {
    tmpfs_unlock(state);
    errno = EACCES;
    return -1;
  }
  if ((amode & W_OK) && !(m & S_IWUSR)) {
    tmpfs_unlock(state);
    errno = EACCES;
    return -1;
  }
  if ((amode & X_OK) && !(m & S_IXUSR)) {
    tmpfs_unlock(state);
    errno = EACCES;
    return -1;
  }

  tmpfs_unlock(state);
  return 0;
}


///////////////////////////////////////////////////////////////////
// Mount & Init
///////////////////////////////////////////////////////////////////

tmpfs_state_t *eos_tmpfs_mount(const char *path) {
  bool prev_k = eos_proc_enter_kernel();
  tmpfs_state_t *state = eos_psram_calloc_prefer(1, sizeof(tmpfs_state_t));
  if (!state) {
    eos_proc_leave_kernel(prev_k);
    EOS_LOGE("Failed to allocate tmpfs state for mount at %s\n", path);
    return NULL;
  }

  state->root = eos_psram_calloc_prefer(1, sizeof(tmpfs_fnode_t));
  if (!state->root) {
    EOS_LOGE("Failed to allocate tmpfs root node for mount at %s\n", path);
    free(state);
    eos_proc_leave_kernel(prev_k);
    return NULL;
  }
  state->root->inode = tmpfs_create_inode(TMP_DIR_MODE);
  if (!state->root->inode) {
    EOS_LOGE("Failed to allocate tmpfs root inode for mount at %s\n", path);
    free(state->root);
    free(state);
    eos_proc_leave_kernel(prev_k);
    return NULL;
  }

  strlcpy(state->root->name, "/", sizeof(state->root->name));
  state->root->nhash = FNV_1A_INITIAL_HVAL;

  pthread_mutexattr_t attr;
  pthread_mutexattr_init(&attr);
  pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
  pthread_mutex_init(&state->lock, &attr);
  pthread_mutexattr_destroy(&attr);

  eos_proc_leave_kernel(prev_k);

  static const eos_vfs_ops_t ops = {
      .open = tmpfs_open,
      .close = tmpfs_close,
      .read = tmpfs_read,
      .write = tmpfs_write,
      .lseek = tmpfs_lseek,
      .fstat = tmpfs_fstat,
      .stat = tmpfs_stat,
      .link = tmpfs_link,
      .unlink = tmpfs_unlink,
      .rename = tmpfs_rename,
      .mkdir = tmpfs_mkdir,
      .rmdir = tmpfs_rmdir,
      .access = tmpfs_access,
      .opendir = tmpfs_opendir,
      .readdir = tmpfs_readdir,
      .closedir = tmpfs_closedir,
      .seekdir = tmpfs_seekdir,
      .telldir = tmpfs_telldir,
      .symlink = tmpfs_symlink,
      .readlink = tmpfs_readlink,
      .lstat = tmpfs_lstat,
  };

  eos_vfs_register_ops(path, &ops, state);
  EOS_LOGI("tmpfs mounted at %s\n", path);

  return state;
}

void eos_tmpfs_init(void) {
  eos_tmpfs_state = eos_tmpfs_mount(EOS_TMPFS_ROOT);
  mkdir("/tmp", 0777);
  mkdir("/proc", 0777);
}
