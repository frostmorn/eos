#include "tmpfs.h"
#include "emisc/fancymacro.h"
#include "emisc/fancytree.h"
#include "emisc/fnv-1a-32.h"
#include "emisc/kvec.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <utime.h>
#include <esp_vfs.h>

///////////////////////////////////////////////////////////////////
// Node
///////////////////////////////////////////////////////////////////

typedef struct {
  char data[TMP_SECTOR_SIZE];
} tmpfs_fblock_t;

typedef struct tmpfs_fnode_t tmpfs_fnode_t;
struct tmpfs_fnode_t {
  EOS_TREE_FIELDS(tmpfs_fnode_t)

  char name[TMP_NODE_NAME_MAX];
  uint32_t nhash;
  mode_t mode;
  time_t atime, mtime, ctime;
  size_t fsize;     // logical size in bytes, <= kv_size(blocks)*TMP_SECTOR_SIZE
  int32_t refcount; // open fds/dirs currently referencing this node
  bool unlinked;    // marked for deletion once refcount drops to 0

  kvec_t(tmpfs_fblock_t) blocks;
};

EOS_TREE_DECLARE(tmpfs_fnode_t);
EOS_TREE_DEFINE(tmpfs_fnode_t)

///////////////////////////////////////////////////////////////////
// Per-mount state
///////////////////////////////////////////////////////////////////

typedef struct {
  tmpfs_fnode_t *pNode;
  int flags;
  long offset;
} tmpfs_fd_t;

typedef struct {
  tmpfs_fnode_t *dir; // the directory node being listed
  tmpfs_fnode_t *cur; // next child to hand back, NULL = exhausted
} tmpfs_dir_t;

typedef struct {
  tmpfs_fnode_t *root;
  kvec_t(tmpfs_fd_t) fds;
} tmpfs_state_t;

#define FD_VALID(state, fd)                                                    \
  ((fd) >= 0 && (fd) < (int)kv_size((state)->fds) &&                           \
   kv_A((state)->fds, fd).pNode)
#define FD_ACCMODE(state, fd) (kv_A((state)->fds, fd).flags & O_ACCMODE)
#define FD_WRITABLE(state, fd)                                                 \
  (FD_ACCMODE(state, fd) == O_WRONLY || FD_ACCMODE(state, fd) == O_RDWR)

///////////////////////////////////////////////////////////////////
// Name matching - hashing itself comes from emisc/fnv-1a-32.h,
// folded per-segment (child's hash seeded from its parent's) so two
// nodes with the same name under different parents never collide,
// and a full strcmp only has to run once the hash already agrees.
///////////////////////////////////////////////////////////////////

static bool tmpfs_match_name(tmpfs_fnode_t *node, void *ctx) {
  return strcmp(node->name, (const char *)ctx) == 0;
}

///////////////////////////////////////////////////////////////////
// Path resolution - segment by segment from root, "." / ".." aware.
// Never a flat/global search: each step only looks at the CURRENT
// node's direct children, so nodes with the same name under
// different parents never collide.
///////////////////////////////////////////////////////////////////

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
      return NULL; // can't possibly match any real node name

    char seg[TMP_NODE_NAME_MAX];
    memcpy(seg, seg_start, seg_len);
    seg[seg_len] = '\0';

    cur = tmpfs_fnode_t_tree_find_child(cur, tmpfs_match_name, seg);
  }

  return cur;
}

// Splits 'path' into its parent node and basename (copied into
// 'namebuf'). Returns NULL (errno set) if the parent doesn't exist,
// the basename is empty, or it's too long to ever match.
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
    return root; // path was "/basename"

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
// Node lifecycle
///////////////////////////////////////////////////////////////////

static tmpfs_fnode_t *tmpfs_create_node(const char *name, mode_t mode,
                                        tmpfs_fnode_t *parent) {
  if (!parent || !name || name[0] == '\0') {
    errno = EINVAL;
    return NULL;
  }
  if (!S_ISDIR(parent->mode)) {
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

  tmpfs_fnode_t *node = malloc(sizeof(tmpfs_fnode_t));
  if (!node) {
    errno = ENOMEM;
    return NULL;
  }
  memset(node, 0, sizeof(tmpfs_fnode_t));

  strlcpy(node->name, name, sizeof(node->name));
  node->nhash = fnv_32a_cstr(name, parent->nhash);
  node->mode = mode;
  node->atime = node->mtime = node->ctime = time(NULL);
  kv_init(node->blocks);

  tmpfs_fnode_t_tree_attach(node, parent);

  return node;
}

static void tmpfs_delete_node(tmpfs_fnode_t *node) {
  if (!node)
    return;
  tmpfs_fnode_t_tree_detach(node);
  kv_destroy(node->blocks);
  free(node);
}

static int tmpfs_alloc_fd(tmpfs_state_t *state, tmpfs_fnode_t *node,
                          int flags) {
  // Reuse a slot left behind by a closed fd, if one exists - avoids
  // the vector growing forever under open/close churn.
  for (size_t i = 0; i < kv_size(state->fds); i++) {
    if (!kv_A(state->fds, i).pNode) {
      kv_A(state->fds, i).pNode = node;
      kv_A(state->fds, i).offset = 0;
      kv_A(state->fds, i).flags = flags;
      node->refcount++;
      return (int)i;
    }
  }

  // No free slot - grow. No artificial cap: how many files a thread
  // can have open at once is bounded by memory, not a fixed count.
  tmpfs_fd_t desc = {.pNode = node, .flags = flags, .offset = 0};
  kv_push(tmpfs_fd_t, state->fds, desc);
  node->refcount++;
  return (int)kv_size(state->fds) - 1;
}

static void tmpfs_fill_stat(const tmpfs_fnode_t *node, struct stat *st) {
  memset(st, 0, sizeof(*st));
  st->st_mode = node->mode;
  st->st_size = (off_t)node->fsize;
  st->st_atime = node->atime;
  st->st_mtime = node->mtime;
  st->st_ctime = node->ctime;
  st->st_nlink = 1;
  st->st_blksize = TMP_SECTOR_SIZE;
  st->st_blocks = (blkcnt_t)kv_size(node->blocks);
}

///////////////////////////////////////////////////////////////////
// VFS operations
///////////////////////////////////////////////////////////////////

static ssize_t tmpfs_write(void *ctx, int fd, const void *data, size_t size) {
  tmpfs_state_t *state = (tmpfs_state_t *)ctx;

  if (!(FD_VALID(state, fd) && FD_WRITABLE(state, fd))) {
    errno = EBADF;
    return -1;
  }
  if (!data) {
    errno = EINVAL;
    return -1;
  }
  if (!size)
    return 0;

  tmpfs_fd_t *desc = &kv_A(state->fds, fd);
  tmpfs_fnode_t *node = desc->pNode;

  if (S_ISDIR(node->mode)) {
    errno = EISDIR;
    return -1;
  }

  if (desc->flags & O_APPEND)
    desc->offset = (long)node->fsize;

  size_t writeEnd = (size_t)desc->offset + size;
  size_t blocksNeeded = (writeEnd + TMP_SECTOR_SIZE - 1) / TMP_SECTOR_SIZE;

  while (kv_size(node->blocks) < blocksNeeded) {
    tmpfs_fblock_t blk;
    memset(&blk, 0, sizeof(blk));
    kv_push(tmpfs_fblock_t, node->blocks, blk);
  }

  size_t remaining = size, srcOff = 0, pos = (size_t)desc->offset;
  const uint8_t *src = (const uint8_t *)data;

  while (remaining > 0) {
    size_t blockIdx = pos / TMP_SECTOR_SIZE;
    size_t byteIdx = pos % TMP_SECTOR_SIZE;
    size_t chunk = TMP_SECTOR_SIZE - byteIdx;
    if (chunk > remaining)
      chunk = remaining;

    memcpy(&kv_A(node->blocks, blockIdx).data[byteIdx], src + srcOff, chunk);

    pos += chunk;
    srcOff += chunk;
    remaining -= chunk;
  }

  node->atime = node->mtime = time(NULL);
  if (writeEnd > node->fsize)
    node->fsize = writeEnd;
  desc->offset = (long)pos;

  return (ssize_t)size;
}

static ssize_t tmpfs_read(void *ctx, int fd, void *dst, size_t size) {
  tmpfs_state_t *state = (tmpfs_state_t *)ctx;

  if (!FD_VALID(state, fd)) {
    errno = EBADF;
    return -1;
  }
  if (!dst) {
    errno = EINVAL;
    return -1;
  }
  if (!size)
    return 0;

  tmpfs_fd_t *desc = &kv_A(state->fds, fd);
  tmpfs_fnode_t *node = desc->pNode;

  if (S_ISDIR(node->mode)) {
    errno = EISDIR;
    return -1;
  }
  if (FD_ACCMODE(state, fd) == O_WRONLY) {
    errno = EBADF;
    return -1;
  }

  if ((size_t)desc->offset >= node->fsize)
    return 0;

  size_t available = node->fsize - (size_t)desc->offset;
  size_t toRead = (size < available) ? size : available;

  size_t remaining = toRead, dstOff = 0, pos = (size_t)desc->offset;
  uint8_t *out = (uint8_t *)dst;

  while (remaining > 0) {
    size_t blockIdx = pos / TMP_SECTOR_SIZE;
    size_t byteIdx = pos % TMP_SECTOR_SIZE;
    size_t chunk = TMP_SECTOR_SIZE - byteIdx;
    if (chunk > remaining)
      chunk = remaining;

    memcpy(out + dstOff, &kv_A(node->blocks, blockIdx).data[byteIdx], chunk);

    pos += chunk;
    dstOff += chunk;
    remaining -= chunk;
  }

  desc->offset += (long)toRead;
  node->atime = time(NULL);

  return (ssize_t)toRead;
}

static off_t tmpfs_lseek(void *ctx, int fd, off_t offset, int whence) {
  tmpfs_state_t *state = (tmpfs_state_t *)ctx;

  if (!FD_VALID(state, fd)) {
    errno = EBADF;
    return -1;
  }

  tmpfs_fd_t *desc = &kv_A(state->fds, fd);
  long newOffset;

  switch (whence) {
  case SEEK_SET:
    newOffset = (long)offset;
    break;
  case SEEK_CUR:
    newOffset = desc->offset + (long)offset;
    break;
  case SEEK_END:
    newOffset = (long)desc->pNode->fsize + (long)offset;
    break;
  default:
    errno = EINVAL;
    return -1;
  }

  if (newOffset < 0) {
    errno = EINVAL;
    return -1;
  }

  desc->offset = newOffset;
  return (off_t)newOffset;
}

static int tmpfs_open(void *ctx, const char *path, int flags, int mode) {
  (void)mode;
  tmpfs_state_t *state = (tmpfs_state_t *)ctx;

  if (!path) {
    errno = EINVAL;
    return -1;
  }

  tmpfs_fnode_t *node = tmpfs_find_node(state->root, path);

  if (node && (flags & O_CREAT) && (flags & O_EXCL)) {
    errno = EEXIST;
    return -1;
  }

  if (!node) {
    if (!(flags & O_CREAT)) {
      errno = ENOENT;
      return -1;
    }

    char name[TMP_NODE_NAME_MAX];
    tmpfs_fnode_t *parent =
        tmpfs_resolve_parent(state->root, path, name, sizeof(name));
    if (!parent)
      return -1;

    node = tmpfs_create_node(name, TMP_FILE_MODE, parent);
    if (!node)
      return -1; // errno already set
  }

  if (S_ISDIR(node->mode)) {
    errno = EISDIR;
    return -1;
  }

  if (flags & O_TRUNC) {
    kv_destroy(node->blocks);
    kv_init(node->blocks);
    node->fsize = 0;
    node->atime = node->ctime = time(NULL);
  }

  int fd = tmpfs_alloc_fd(state, node, flags);
  if (fd < 0)
    return -1;

  if (flags & O_APPEND)
    kv_A(state->fds, fd).offset = (long)node->fsize;

  return fd;
}

static int tmpfs_close(void *ctx, int fd) {
  tmpfs_state_t *state = (tmpfs_state_t *)ctx;

  if (!FD_VALID(state, fd)) {
    errno = EBADF;
    return -1;
  }

  tmpfs_fnode_t *node = kv_A(state->fds, fd).pNode;
  node->refcount--;
  memset(&kv_A(state->fds, fd), 0, sizeof(tmpfs_fd_t));

  if (node->unlinked && node->refcount == 0)
    tmpfs_delete_node(node);

  return 0;
}

static int tmpfs_stat(void *ctx, const char *path, struct stat *st) {
  tmpfs_state_t *state = (tmpfs_state_t *)ctx;

  if (!path || !st) {
    errno = EINVAL;
    return -1;
  }

  tmpfs_fnode_t *node = tmpfs_find_node(state->root, path);
  if (!node) {
    errno = ENOENT;
    return -1;
  }

  tmpfs_fill_stat(node, st);
  return 0;
}

static int tmpfs_fstat(void *ctx, int fd, struct stat *st) {
  tmpfs_state_t *state = (tmpfs_state_t *)ctx;

  if (!FD_VALID(state, fd)) {
    errno = EBADF;
    return -1;
  }
  if (!st) {
    errno = EINVAL;
    return -1;
  }

  tmpfs_fill_stat(kv_A(state->fds, fd).pNode, st);
  return 0;
}

static int tmpfs_unlink(void *ctx, const char *path) {
  tmpfs_state_t *state = (tmpfs_state_t *)ctx;

  if (!path) {
    errno = EINVAL;
    return -1;
  }

  tmpfs_fnode_t *node = tmpfs_find_node(state->root, path);
  if (!node) {
    errno = ENOENT;
    return -1;
  }
  if (node == state->root) {
    errno = EBUSY;
    return -1;
  }
  if (S_ISDIR(node->mode) && node->child) {
    errno = ENOTEMPTY;
    return -1;
  }

  // Detach immediately - the name must stop resolving right away,
  // even if a still-open fd keeps the underlying data alive a while
  // longer. tmpfs_delete_node()'s own tree_detach() is a safe no-op
  // if called again once refcount actually reaches 0 below.
  tmpfs_fnode_t_tree_detach(node);

  if (node->refcount) {
    node->unlinked = true;
  } else {
    tmpfs_delete_node(node);
  }

  return 0;
}

static int tmpfs_rename(void *ctx, const char *src, const char *dst) {
  tmpfs_state_t *state = (tmpfs_state_t *)ctx;

  if (!src || !dst) {
    errno = EINVAL;
    return -1;
  }

  tmpfs_fnode_t *node = tmpfs_find_node(state->root, src);
  if (!node) {
    errno = ENOENT;
    return -1;
  }
  if (node == state->root) {
    errno = EBUSY;
    return -1;
  }

  char name[TMP_NODE_NAME_MAX];
  tmpfs_fnode_t *dstParent =
      tmpfs_resolve_parent(state->root, dst, name, sizeof(name));
  if (!dstParent)
    return -1;

  tmpfs_fnode_t *dstNode =
      tmpfs_fnode_t_tree_find_child(dstParent, tmpfs_match_name, name);
  if (dstNode) {
    if (S_ISDIR(dstNode->mode) && dstNode->child) {
      errno = ENOTEMPTY;
      return -1;
    }
    if (S_ISDIR(node->mode) != S_ISDIR(dstNode->mode)) {
      errno = S_ISDIR(node->mode) ? ENOTDIR : EISDIR;
      return -1;
    }
    tmpfs_delete_node(dstNode);
  }

  tmpfs_fnode_t_tree_detach(node);

  strlcpy(node->name, name, sizeof(node->name));
  node->nhash = fnv_32a_cstr(name, dstParent->nhash);
  node->mtime = node->ctime = time(NULL);

  tmpfs_fnode_t_tree_attach(node, dstParent);

  return 0;
}

static DIR *tmpfs_opendir(void *ctx, const char *path) {
  tmpfs_state_t *state = (tmpfs_state_t *)ctx;

  tmpfs_fnode_t *node = tmpfs_find_node(state->root, path);
  if (!node) {
    errno = ENOENT;
    return NULL;
  }
  if (!S_ISDIR(node->mode)) {
    errno = ENOTDIR;
    return NULL;
  }

  tmpfs_dir_t *dir = malloc(sizeof(tmpfs_dir_t));
  if (!dir) {
    errno = ENOMEM;
    return NULL;
  }

  dir->dir = node;
  dir->cur = node->child;
  node->refcount++;

  return (DIR *)dir;
}

static struct dirent *tmpfs_readdir(void *ctx, DIR *pdir) {
  (void)ctx;
  if (!pdir) {
    errno = EINVAL;
    return NULL;
  }

  tmpfs_dir_t *dir = (tmpfs_dir_t *)pdir;
  tmpfs_fnode_t *node = dir->cur;
  if (!node)
    return NULL; // end of directory

  dir->cur = node->next;

  static struct dirent entry;
  memset(&entry, 0, sizeof(entry));
  entry.d_ino = (ino_t)(uintptr_t)node;
  entry.d_type = S_ISDIR(node->mode) ? DT_DIR : DT_REG;
  strlcpy(entry.d_name, node->name, sizeof(entry.d_name));

  return &entry;
}

static long tmpfs_telldir(void *ctx, DIR *pdir) {
  (void)ctx;
  if (!pdir) {
    errno = EINVAL;
    return -1;
  }
  tmpfs_dir_t *dir = (tmpfs_dir_t *)pdir;
  return (long)(uintptr_t)dir->cur;
}

static void tmpfs_seekdir(void *ctx, DIR *pdir, long offset) {
  (void)ctx;
  if (!pdir)
    return;

  tmpfs_dir_t *dir = (tmpfs_dir_t *)pdir;

  // esp-idf's rewinddir() calls seekdir(dir, 0). Since 'offset' here
  // is repurposed as an opaque node pointer (from telldir), 0 has to
  // mean "back to the first child" rather than a literal NULL node -
  // matching the reference implementation's own workaround for the
  // same quirk.
  tmpfs_fnode_t *target = (tmpfs_fnode_t *)(uintptr_t)offset;
  dir->cur = target ? target : dir->dir->child;
}

static int tmpfs_closedir(void *ctx, DIR *pdir) {
  (void)ctx;
  if (!pdir) {
    errno = EINVAL;
    return -1;
  }

  tmpfs_dir_t *dir = (tmpfs_dir_t *)pdir;
  tmpfs_fnode_t *node = dir->dir;
  node->refcount--;
  free(dir);

  if (node->unlinked && node->refcount == 0)
    tmpfs_delete_node(node);

  return 0;
}

static int tmpfs_mkdir(void *ctx, const char *path, mode_t mode) {
  (void)mode;
  tmpfs_state_t *state = (tmpfs_state_t *)ctx;

  if (!path) {
    errno = EINVAL;
    return -1;
  }

  char name[TMP_NODE_NAME_MAX];
  tmpfs_fnode_t *parent =
      tmpfs_resolve_parent(state->root, path, name, sizeof(name));
  if (!parent)
    return -1;

  tmpfs_fnode_t *node = tmpfs_create_node(name, TMP_DIR_MODE, parent);
  if (!node)
    return -1;

  return 0;
}

static int tmpfs_rmdir(void *ctx, const char *path) {
  tmpfs_state_t *state = (tmpfs_state_t *)ctx;

  if (!path) {
    errno = EINVAL;
    return -1;
  }

  tmpfs_fnode_t *node = tmpfs_find_node(state->root, path);
  if (!node) {
    errno = ENOENT;
    return -1;
  }
  if (!S_ISDIR(node->mode)) {
    errno = ENOTDIR;
    return -1;
  }
  if (node == state->root) {
    errno = EBUSY;
    return -1;
  }
  if (node->child) {
    errno = ENOTEMPTY;
    return -1;
  }
  if (node->refcount) {
    errno = EBUSY;
    return -1;
  }

  tmpfs_delete_node(node);
  return 0;
}

static int tmpfs_access(void *ctx, const char *path, int amode) {
  tmpfs_state_t *state = (tmpfs_state_t *)ctx;

  if (!path) {
    errno = EINVAL;
    return -1;
  }

  tmpfs_fnode_t *node = tmpfs_find_node(state->root, path);
  if (!node) {
    errno = ENOENT;
    return -1;
  }

  if (amode == F_OK)
    return 0;

  mode_t m = node->mode;
  if ((amode & R_OK) && !(m & S_IRUSR)) {
    errno = EACCES;
    return -1;
  }
  if ((amode & W_OK) && !(m & S_IWUSR)) {
    errno = EACCES;
    return -1;
  }
  if ((amode & X_OK) && !(m & S_IXUSR)) {
    errno = EACCES;
    return -1;
  }

  return 0;
}

static int tmpfs_utime(void *ctx, const char *path,
                       const struct utimbuf *times) {
  tmpfs_state_t *state = (tmpfs_state_t *)ctx;

  if (!path) {
    errno = EINVAL;
    return -1;
  }

  tmpfs_fnode_t *node = tmpfs_find_node(state->root, path);
  if (!node) {
    errno = ENOENT;
    return -1;
  }

  if (times) {
    node->atime = times->actime;
    node->mtime = times->modtime;
  } else {
    node->atime = node->mtime = time(NULL);
  }

  return 0;
}

///////////////////////////////////////////////////////////////////
// Mount
///////////////////////////////////////////////////////////////////

void eos_tmpfs_mount(const char *path) {
  tmpfs_state_t *state = malloc(sizeof(tmpfs_state_t));
  if (!state) {
    EOS_LOGE("Failed to allocate tmpfs state for mount at %s\n", path);
    return;
  }
  memset(state, 0, sizeof(tmpfs_state_t));

  state->root = malloc(sizeof(tmpfs_fnode_t));
  if (!state->root) {
    EOS_LOGE("Failed to allocate tmpfs root node for mount at %s\n", path);
    free(state);
    return;
  }
  memset(state->root, 0, sizeof(tmpfs_fnode_t));
  strlcpy(state->root->name, "/", sizeof(state->root->name));
  state->root->nhash = FNV_1A_INITIAL_HVAL;
  state->root->mode = TMP_DIR_MODE;
  state->root->atime = state->root->mtime = state->root->ctime = time(NULL);
  kv_init(state->root->blocks);
  // root->parent/child/next stay NULL from the memset above - no
  // self-referential-root trick needed, since tmpfs_find_node checks
  // node->parent directly for ".." and treats NULL as "stay put".

  static const esp_vfs_t vfs = {
      .flags = ESP_VFS_FLAG_CONTEXT_PTR,
      .open_p = tmpfs_open,
      .close_p = tmpfs_close,
      .read_p = tmpfs_read,
      .write_p = tmpfs_write,
      .lseek_p = tmpfs_lseek,
      .stat_p = tmpfs_stat,
      .fstat_p = tmpfs_fstat,
      .unlink_p = tmpfs_unlink,
      .rename_p = tmpfs_rename,
      .opendir_p = tmpfs_opendir,
      .readdir_p = tmpfs_readdir,
      .seekdir_p = tmpfs_seekdir,
      .telldir_p = tmpfs_telldir,
      .closedir_p = tmpfs_closedir,
      .mkdir_p = tmpfs_mkdir,
      .rmdir_p = tmpfs_rmdir,
      .access_p = tmpfs_access,
      .utime_p = tmpfs_utime,
  };

  esp_vfs_register(path, &vfs, state);
  EOS_LOGI("tmpfs mounted at %s\n", path);
}