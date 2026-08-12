#include "emisc/fancymacro.h"
#include "rootfs.h"
#include <dirent.h>
#include <errno.h>
#include <esp_vfs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

// ── Dir state ─────────────────────────────────────────────────

typedef struct {
  uint32_t esp_idf_fs_index; // must be first
  int idx;                   // current position in path list
} rootfs_dir_t;

// ── Path discovery from ESP-IDF VFS table ─────────────────────

// Parse esp_vfs_dump output to extract registered paths
// Format: "index:path -> ptr\n"
static int rootfs_get_paths(char paths[][ESP_VFS_PATH_MAX + 1], int max) {
  // dump to a memory buffer via open_memstream
  char *buf = NULL;
  size_t buf_len = 0;
  FILE *f = open_memstream(&buf, &buf_len);
  if (!f)
    return 0;

  esp_vfs_dump_registered_paths(f);
  fflush(f);
  fclose(f);

  if (!buf)
    return 0;

  int count = 0;
  char *line = buf;
  char *next;

  while (line && count < max) {
    next = strchr(line, '\n');
    if (next)
      *next++ = '\0';

    // format: "  0:/dev -> 0x..." or "  1:/sd -> 0x..."
    // find the colon, then extract path up to space
    char *colon = strchr(line, ':');
    if (colon) {
      char *path_start = colon + 1;
      char *path_end = strchr(path_start, ' ');
      if (path_end)
        *path_end = '\0';

      // skip NULL entries and root
      if (strcmp(path_start, "NULL") != 0 && strlen(path_start) > 1) {
        strlcpy(paths[count], path_start, ESP_VFS_PATH_MAX + 1);
        count++;
      }
    }

    line = next;
  }

  free(buf);
  return count;
}

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
  if (strcmp(path, "/") == 0 || strcmp(path, "") == 0) {
    memset(st, 0, sizeof(*st));
    st->st_mode = S_IFDIR | 0555;
    return 0;
  }

  // check if path matches a registered VFS prefix
  char paths[CONFIG_LWIP_MAX_SOCKETS][ESP_VFS_PATH_MAX + 1];
  int count = rootfs_get_paths(paths, CONFIG_LWIP_MAX_SOCKETS);

  for (int i = 0; i < count; i++) {
    // strip leading slash from path for comparison
    const char *name = path[0] == '/' ? path + 1 : path;
    const char *entry = paths[i][0] == '/' ? paths[i] + 1 : paths[i];
    // match top-level component only
    char entry_top[ESP_VFS_PATH_MAX + 1];
    strlcpy(entry_top, entry, sizeof(entry_top));
    char *slash = strchr(entry_top, '/');
    if (slash)
      *slash = '\0';

    if (strcmp(name, entry_top) == 0) {
      memset(st, 0, sizeof(*st));
      st->st_mode = S_IFDIR | 0555;
      return 0;
    }
  }

  errno = ENOENT;
  return -1;
}

static DIR *rootfs_opendir(void *ctx, const char *path) {
  if (strcmp(path, "/") != 0 && strcmp(path, "") != 0) {
    errno = ENOENT;
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

  // get fresh path list on each readdir sequence
  // (rewind resets idx to 0 so first readdir re-scans)
  static char s_paths[16][ESP_VFS_PATH_MAX + 1];
  static int s_count = 0;
  static struct dirent s_entry;

  if (dir->idx == 0)
    s_count = rootfs_get_paths(s_paths, 16);

  // deduplicate — expose only top-level components
  // e.g. /dev, /sd, /bin — not /dev/spi0 separately
  char seen[16][ESP_VFS_PATH_MAX + 1];
  int seen_count = 0;

  // collect top-level unique names up to current idx
  int real_idx = 0;
  for (int i = 0; i < s_count; i++) {
    char top[ESP_VFS_PATH_MAX + 1];
    const char *p = s_paths[i][0] == '/' ? s_paths[i] + 1 : s_paths[i];
    strlcpy(top, p, sizeof(top));
    char *slash = strchr(top, '/');
    if (slash)
      *slash = '\0';

    // check if already seen
    bool dup = false;
    for (int j = 0; j < seen_count; j++) {
      if (strcmp(seen[j], top) == 0) {
        dup = true;
        break;
      }
    }
    if (dup)
      continue;
    strlcpy(seen[seen_count++], top, sizeof(seen[0]));

    if (real_idx == dir->idx) {
      memset(&s_entry, 0, sizeof(s_entry));
      s_entry.d_type = DT_DIR;
      s_entry.d_ino = (ino_t)(i + 1);
      strlcpy(s_entry.d_name, top, sizeof(s_entry.d_name));
      dir->idx++;
      return &s_entry;
    }
    real_idx++;
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

// ── Init ──────────────────────────────────────────────────────

void eos_rootfs_init(void) {
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
  EOS_LOGI("rootfs: mounted at ");
}