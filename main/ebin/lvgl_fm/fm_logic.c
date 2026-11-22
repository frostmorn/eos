#include "fm_logic.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

void fm_logic_join_path(char *dst, size_t dst_len, const char *base, const char *sub) {
  if (!dst || dst_len == 0) return;
  if (!sub || sub[0] == '\0') {
    strncpy(dst, base ? base : "/", dst_len - 1);
    dst[dst_len - 1] = '\0';
    return;
  }
  if (sub[0] == '/') {
    strncpy(dst, sub, dst_len - 1);
    dst[dst_len - 1] = '\0';
    return;
  }
  if (!base || strcmp(base, "/") == 0) {
    snprintf(dst, dst_len, "/%s", sub);
  } else {
    snprintf(dst, dst_len, "%s/%s", base, sub);
  }
}

void fm_logic_parent_path(const char *path, char *buf, size_t buflen) {
  if (!buf || buflen == 0) return;
  if (!path || strcmp(path, "/") == 0 || path[0] == '\0') {
    strncpy(buf, "/", buflen - 1);
    buf[buflen - 1] = '\0';
    return;
  }

  strncpy(buf, path, buflen - 1);
  buf[buflen - 1] = '\0';

  char *last_slash = strrchr(buf, '/');
  if (!last_slash || last_slash == buf) {
    strncpy(buf, "/", buflen - 1);
    buf[buflen - 1] = '\0';
  } else {
    *last_slash = '\0';
  }
}

void fm_logic_get_full_path(const fm_data_t *d, size_t index, char *buf, size_t buflen) {
  if (!buf || buflen == 0) return;
  buf[0] = '\0';
  if (!d || index >= kv_size(d->entries)) return;

  const fm_entry_t *e = &kv_A(d->entries, index);
  fm_logic_join_path(buf, buflen, d->cwd, e->name);
}

bool fm_logic_is_dir(const fm_entry_t *e) {
  if (!e) return false;
  return S_ISDIR(e->stat.st_mode);
}

bool fm_logic_is_exec(const fm_entry_t *e) {
  if (!e || S_ISDIR(e->stat.st_mode)) return false;
  return (e->stat.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)) != 0;
}

const char *fm_logic_get_icon(const fm_entry_t *e) {
  if (!e) return "📄";
  if (strcmp(e->name, "..") == 0 || fm_logic_is_dir(e)) {
    return "📁";
  }
  if (fm_logic_is_exec(e)) {
    return "▶️";
  }

  const char *dot = strrchr(e->name, '.');
  if (dot) {
    if (strcasecmp(dot, ".png") == 0 || strcasecmp(dot, ".bmp") == 0 ||
        strcasecmp(dot, ".jpg") == 0) {
      return "🖼️";
    }
    if (strcasecmp(dot, ".mp3") == 0 || strcasecmp(dot, ".wav") == 0) {
      return "🎵";
    }
  }
  return "📄";
}

void fm_logic_format_size(off_t size, char *buf, size_t buflen) {
  if (!buf || buflen == 0) return;
  if (size < (off_t)FM_BYTES_PER_KB) {
    snprintf(buf, buflen, "%ld B", (long)size);
  } else if (size < (off_t)FM_BYTES_PER_MB) {
    double kb = (double)size / (double)FM_BYTES_PER_KB;
    snprintf(buf, buflen, "%.1f KB", kb);
  } else {
    double mb = (double)size / (double)FM_BYTES_PER_MB;
    snprintf(buf, buflen, "%.1f MB", mb);
  }
}

static int fm_entry_cmp(const void *p1, const void *p2) {
  const fm_entry_t *e1 = (const fm_entry_t *)p1;
  const fm_entry_t *e2 = (const fm_entry_t *)p2;

  // ".." always precedes everything
  if (strcmp(e1->name, "..") == 0) return -1;
  if (strcmp(e2->name, "..") == 0) return 1;

  bool dir1 = fm_logic_is_dir(e1);
  bool dir2 = fm_logic_is_dir(e2);

  if (dir1 && !dir2) return -1;
  if (!dir1 && dir2) return 1;

  return strcasecmp(e1->name, e2->name);
}

void fm_logic_sort(fm_data_t *d) {
  if (!d || kv_size(d->entries) <= 1) return;
  qsort(d->entries.a, kv_size(d->entries), sizeof(fm_entry_t), fm_entry_cmp);
}

int fm_logic_init(fm_data_t *d, const char *initial_path) {
  if (!d) return -1;
  kv_init(d->entries);
  d->cwd[0] = '\0';

  const char *start = initial_path ? initial_path : "/";
  return fm_logic_load_dir(d, start);
}

void fm_logic_deinit(fm_data_t *d) {
  if (!d) return;
  kv_destroy(d->entries);
  d->entries.a = NULL;
  d->entries.n = 0;
  d->entries.m = 0;
  d->cwd[0] = '\0';
}

int fm_logic_load_dir(fm_data_t *d, const char *path) {
  if (!d || !path) return -1;

  DIR *dir = opendir(path);
  if (!dir) return -1;

  kv_destroy(d->entries);
  kv_init(d->entries);

  strncpy(d->cwd, path, sizeof(d->cwd) - 1);
  d->cwd[sizeof(d->cwd) - 1] = '\0';

  // If not at root, insert parent navigation entry ".."
  if (strcmp(d->cwd, "/") != 0) {
    fm_entry_t parent_entry;
    memset(&parent_entry, 0, sizeof(parent_entry));
    strncpy(parent_entry.name, "..", sizeof(parent_entry.name) - 1);
    parent_entry.stat.st_mode = S_IFDIR | EOS_DEFAULT_DIR_MODE;
    kv_push(fm_entry_t, d->entries, parent_entry);
  }

  struct dirent *ent = NULL;
  while ((ent = readdir(dir)) != NULL) {
    if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
      continue;
    }

    fm_entry_t e;
    memset(&e, 0, sizeof(e));
    strncpy(e.name, ent->d_name, sizeof(e.name) - 1);

    char full[EOS_MID_STR_LEN];
    fm_logic_join_path(full, sizeof(full), d->cwd, ent->d_name);

    if (stat(full, &e.stat) != 0) {
      if (ent->d_type == DT_DIR) {
        e.stat.st_mode |= S_IFDIR;
      } else {
        e.stat.st_mode |= S_IFREG;
      }
    }

    kv_push(fm_entry_t, d->entries, e);
  }

  closedir(dir);
  fm_logic_sort(d);
  return 0;
}

int fm_logic_refresh(fm_data_t *d) {
  if (!d || d->cwd[0] == '\0') return -1;
  char cur[EOS_MID_STR_LEN];
  strncpy(cur, d->cwd, sizeof(cur) - 1);
  cur[sizeof(cur) - 1] = '\0';
  return fm_logic_load_dir(d, cur);
}

int fm_logic_navigate_up(fm_data_t *d) {
  if (!d) return -1;
  char parent[EOS_MID_STR_LEN];
  fm_logic_parent_path(d->cwd, parent, sizeof(parent));
  return fm_logic_load_dir(d, parent);
}

int fm_logic_delete_entry(fm_data_t *d, size_t index) {
  if (!d || index >= kv_size(d->entries)) return -1;

  char full[EOS_MID_STR_LEN];
  fm_logic_get_full_path(d, index, full, sizeof(full));

  const fm_entry_t *e = &kv_A(d->entries, index);
  int ret = 0;
  if (fm_logic_is_dir(e)) {
    ret = rmdir(full);
  } else {
    ret = unlink(full);
  }

  if (ret == 0) {
    fm_logic_refresh(d);
  }
  return ret;
}

int fm_logic_create_folder(fm_data_t *d, const char *folder_name) {
  if (!d || !folder_name || folder_name[0] == '\0') return -1;

  char full[EOS_MID_STR_LEN];
  fm_logic_join_path(full, sizeof(full), d->cwd, folder_name);

  int ret = mkdir(full, EOS_DEFAULT_DIR_MODE);
  if (ret == 0) {
    fm_logic_refresh(d);
  }
  return ret;
}
