#include "ecore/bin.h"
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

extern int lstat(const char *path, struct stat *st);

// Forward declarations of ebin entry points
extern int sleep_main(int argc, char **argv);
extern int rm_main(int argc, char **argv);
extern int cp_main(int argc, char **argv);
extern int mv_main(int argc, char **argv);
extern int tree_main(int argc, char **argv);
extern int ln_main(int argc, char **argv);
extern int readlink_main(int argc, char **argv);

typedef struct {
  int passed;
  int failed;
} test_ctx_t;

static void report_test(test_ctx_t *ctx, const char *name, bool ok) {
  if (ok) {
    printf("  [PASS] %s\n", name);
    ctx->passed++;
  } else {
    printf("  [FAIL] %s\n", name);
    ctx->failed++;
  }
}

static bool helper_write_file(const char *path, const char *content) {
  FILE *f = fopen(path, "wb");
  if (!f) return false;
  size_t len = strlen(content);
  size_t w = fwrite(content, 1, len, f);
  fclose(f);
  return (w == len);
}

static bool helper_read_matches(const char *path, const char *expected) {
  FILE *f = fopen(path, "rb");
  if (!f) return false;
  char buf[256];
  size_t n = fread(buf, 1, sizeof(buf) - 1, f);
  fclose(f);
  buf[n] = '\0';
  return (strcmp(buf, expected) == 0);
}

static bool helper_path_exists(const char *path) {
  struct stat st;
  return (stat(path, &st) == 0);
}

// ── 1. Sleep Tests ─────────────────────────────────────────────

static void test_sleep_suite(test_ctx_t *ctx) {
  printf("--- sleep Tests ---\n");

  {
    char *argv[] = {"sleep", NULL};
    int ret = sleep_main(1, argv);
    report_test(ctx, "sleep: no arguments returns error", ret != 0);
  }

  {
    char *argv[] = {"sleep", "--help", NULL};
    int ret = sleep_main(2, argv);
    report_test(ctx, "sleep: --help returns 0", ret == 0);
  }

  {
    char *argv[] = {"sleep", "-1", NULL};
    int ret = sleep_main(2, argv);
    report_test(ctx, "sleep: negative interval returns error", ret != 0);
  }

  {
    char *argv[] = {"sleep", "invalid_text", NULL};
    int ret = sleep_main(2, argv);
    report_test(ctx, "sleep: invalid string returns error", ret != 0);
  }

  {
    char *argv[] = {"sleep", "0", NULL};
    int ret = sleep_main(2, argv);
    report_test(ctx, "sleep: 0 seconds returns 0", ret == 0);
  }

  {
    char *argv[] = {"sleep", "10ms", NULL};
    int ret = sleep_main(2, argv);
    report_test(ctx, "sleep: 10ms suffix returns 0", ret == 0);
  }

  {
    char *argv[] = {"sleep", "0.005", NULL};
    int ret = sleep_main(2, argv);
    report_test(ctx, "sleep: fractional seconds (0.005) returns 0", ret == 0);
  }

  {
    char *argv[] = {"sleep", "5ms", "5ms", NULL};
    int ret = sleep_main(3, argv);
    report_test(ctx, "sleep: multiple interval arguments return 0", ret == 0);
  }
}

// ── 2. Rm Tests ────────────────────────────────────────────────

static void test_rm_suite(test_ctx_t *ctx) {
  printf("--- rm Tests ---\n");

  {
    char *argv[] = {"rm", NULL};
    int ret = rm_main(1, argv);
    report_test(ctx, "rm: no arguments returns error", ret != 0);
  }

  {
    char *argv[] = {"rm", "-h", NULL};
    int ret = rm_main(2, argv);
    report_test(ctx, "rm: -h help returns 0", ret == 0);
  }

  {
    char *argv[] = {"rm", "/tmp/no_file.txt", NULL};
    int ret = rm_main(2, argv);
    report_test(ctx, "rm: non-existent file returns error", ret != 0);
  }

  {
    char *argv[] = {"rm", "-f", "/tmp/no_file.txt", NULL};
    int ret = rm_main(3, argv);
    report_test(ctx, "rm: -f non-existent file succeeds silently", ret == 0);
  }

  // Single file removal
  {
    const char *path = "/tmp/rm_file.txt";
    helper_write_file(path, "test rm content");
    report_test(ctx, "rm: file created successfully before test", helper_path_exists(path));

    char *argv[] = {"rm", (char *)path, NULL};
    int ret = rm_main(2, argv);
    report_test(ctx, "rm: single file removal returns 0", ret == 0);
    report_test(ctx, "rm: file is verified unlinked", !helper_path_exists(path));
  }

  // Multiple files removal
  {
    const char *f1 = "/tmp/rm_m1.txt";
    const char *f2 = "/tmp/rm_m2.txt";
    helper_write_file(f1, "1");
    helper_write_file(f2, "2");

    char *argv[] = {"rm", "-v", (char *)f1, (char *)f2, NULL};
    int ret = rm_main(4, argv);
    report_test(ctx, "rm: multiple file removal returns 0", ret == 0);
    report_test(ctx, "rm: multi f1 unlinked", !helper_path_exists(f1));
    report_test(ctx, "rm: multi f2 unlinked", !helper_path_exists(f2));
  }

  // Directory removal without -r fails
  {
    const char *dir = "/tmp/rm_nodir";
    mkdir(dir, 0755);
    char *argv[] = {"rm", (char *)dir, NULL};
    int ret = rm_main(2, argv);
    report_test(ctx, "rm: directory without -r returns error (Is a directory)", ret != 0);
    report_test(ctx, "rm: directory was preserved", helper_path_exists(dir));
    rmdir(dir);
  }

  // Recursive directory removal with -r
  {
    const char *dir = "/tmp/rm_tree";
    const char *subdir = "/tmp/rm_tree/sub";
    const char *file1 = "/tmp/rm_tree/f1.txt";
    const char *file2 = "/tmp/rm_tree/sub/f2.txt";
    mkdir(dir, 0755);
    mkdir(subdir, 0755);
    helper_write_file(file1, "f1");
    helper_write_file(file2, "f2");

    char *argv[] = {"rm", "-r", (char *)dir, NULL};
    int ret = rm_main(3, argv);
    report_test(ctx, "rm: recursive directory removal with -r returns 0", ret == 0);
    report_test(ctx, "rm: recursive tree completely removed", !helper_path_exists(dir));
  }

  // Combined -rf flags
  {
    const char *dir = "/tmp/rm_rf";
    mkdir(dir, 0755);
    char *argv[] = {"rm", "-rf", (char *)dir, NULL};
    int ret = rm_main(3, argv);
    report_test(ctx, "rm: -rf returns 0", ret == 0);
    report_test(ctx, "rm: -rf directory removed", !helper_path_exists(dir));
  }
}

// ── 3. Cp Tests ────────────────────────────────────────────────

static void test_cp_suite(test_ctx_t *ctx) {
  printf("--- cp Tests ---\n");

  {
    char *argv[] = {"cp", NULL};
    int ret = cp_main(1, argv);
    report_test(ctx, "cp: no arguments returns error", ret != 0);
  }

  {
    char *argv[] = {"cp", "-h", NULL};
    int ret = cp_main(2, argv);
    report_test(ctx, "cp: -h help returns 0", ret == 0);
  }

  {
    char *argv[] = {"cp", "/tmp/no_src.txt", "/tmp/dst_1234", NULL};
    int ret = cp_main(3, argv);
    report_test(ctx, "cp: non-existent source returns error", ret != 0);
  }

  // Same file detection
  {
    const char *f = "/tmp/cp_same.txt";
    helper_write_file(f, "content");
    char *argv[] = {"cp", (char *)f, (char *)f, NULL};
    int ret = cp_main(3, argv);
    report_test(ctx, "cp: same file copy returns error", ret != 0);
    unlink(f);
  }

  // Single file copy
  {
    const char *src = "/tmp/cp_src.txt";
    const char *dst = "/tmp/cp_dst.txt";
    const char *data = "The quick brown fox jumps over the lazy dog";
    helper_write_file(src, data);

    char *argv[] = {"cp", (char *)src, (char *)dst, NULL};
    int ret = cp_main(3, argv);
    report_test(ctx, "cp: single file copy returns 0", ret == 0);
    report_test(ctx, "cp: destination file exists", helper_path_exists(dst));
    report_test(ctx, "cp: destination content matches source", helper_read_matches(dst, data));
    report_test(ctx, "cp: source file still exists", helper_path_exists(src));

    unlink(src);
    unlink(dst);
  }

  // Copy into existing directory
  {
    const char *src = "/tmp/cp_item.txt";
    const char *dir = "/tmp/cp_dir";
    const char *expected_dst = "/tmp/cp_dir/cp_item.txt";
    const char *data = "Item in directory";
    helper_write_file(src, data);
    mkdir(dir, 0755);

    char *argv[] = {"cp", (char *)src, (char *)dir, NULL};
    int ret = cp_main(3, argv);
    report_test(ctx, "cp: copy file into directory returns 0", ret == 0);
    report_test(ctx, "cp: file exists in target directory", helper_path_exists(expected_dst));
    report_test(ctx, "cp: file content inside directory matches", helper_read_matches(expected_dst, data));

    unlink(expected_dst);
    unlink(src);
    rmdir(dir);
  }

  // Multiple files into directory
  {
    const char *f1 = "/tmp/cp_m1.txt";
    const char *f2 = "/tmp/cp_m2.txt";
    const char *dir = "/tmp/cp_mdir";
    helper_write_file(f1, "multi 1");
    helper_write_file(f2, "multi 2");
    mkdir(dir, 0755);

    char *argv[] = {"cp", (char *)f1, (char *)f2, (char *)dir, NULL};
    int ret = cp_main(4, argv);
    report_test(ctx, "cp: multiple files to directory returns 0", ret == 0);
    report_test(ctx, "cp: target f1 exists", helper_path_exists("/tmp/cp_mdir/cp_m1.txt"));
    report_test(ctx, "cp: target f2 exists", helper_path_exists("/tmp/cp_mdir/cp_m2.txt"));

    unlink("/tmp/cp_mdir/cp_m1.txt");
    unlink("/tmp/cp_mdir/cp_m2.txt");
    unlink(f1);
    unlink(f2);
    rmdir(dir);
  }

  // Directory copy without -r fails
  {
    const char *src_dir = "/tmp/cp_sdir";
    const char *dst_dir = "/tmp/cp_ddir";
    mkdir(src_dir, 0755);

    char *argv[] = {"cp", (char *)src_dir, (char *)dst_dir, NULL};
    int ret = cp_main(3, argv);
    report_test(ctx, "cp: directory copy without -r returns error", ret != 0);

    rmdir(src_dir);
  }

  // Recursive directory copy with -r
  {
    const char *src_dir = "/tmp/cp_tsrc";
    const char *src_sub = "/tmp/cp_tsrc/sub";
    const char *src_file = "/tmp/cp_tsrc/sub/deep.txt";
    const char *dst_dir = "/tmp/cp_tdst";
    const char *dst_file = "/tmp/cp_tdst/sub/deep.txt";
    const char *deep_data = "Deep nested data payload 4096";

    mkdir(src_dir, 0755);
    mkdir(src_sub, 0755);
    helper_write_file(src_file, deep_data);

    char *argv[] = {"cp", "-r", (char *)src_dir, (char *)dst_dir, NULL};
    int ret = cp_main(4, argv);
    report_test(ctx, "cp: recursive directory copy -r returns 0", ret == 0);
    report_test(ctx, "cp: destination directory created", helper_path_exists(dst_dir));
    report_test(ctx, "cp: nested file copied properly", helper_path_exists(dst_file));
    report_test(ctx, "cp: nested file content matches", helper_read_matches(dst_file, deep_data));

    // Clean up
    unlink(src_file);
    rmdir(src_sub);
    rmdir(src_dir);
    unlink(dst_file);
    rmdir("/tmp/cp_tdst/sub");
    rmdir(dst_dir);
  }

  // Overwriting with -f
  {
    const char *src = "/tmp/cp_ovs.txt";
    const char *dst = "/tmp/cp_ovd.txt";
    helper_write_file(src, "NEW DATA");
    helper_write_file(dst, "OLD DATA");

    char *argv[] = {"cp", "-f", (char *)src, (char *)dst, NULL};
    int ret = cp_main(4, argv);
    report_test(ctx, "cp: overwrite with -f returns 0", ret == 0);
    report_test(ctx, "cp: destination updated with new content", helper_read_matches(dst, "NEW DATA"));

    unlink(src);
    unlink(dst);
  }
}

// ── 4. Mv Tests ────────────────────────────────────────────────

static void test_mv_suite(test_ctx_t *ctx) {
  printf("--- mv Tests ---\n");

  {
    char *argv[] = {"mv", NULL};
    int ret = mv_main(1, argv);
    report_test(ctx, "mv: no arguments returns error", ret != 0);
  }

  {
    char *argv[] = {"mv", "-h", NULL};
    int ret = mv_main(2, argv);
    report_test(ctx, "mv: -h help returns 0", ret == 0);
  }

  {
    char *argv[] = {"mv", "/tmp/mv_no.txt", "/tmp/dest", NULL};
    int ret = mv_main(3, argv);
    report_test(ctx, "mv: non-existent source returns error", ret != 0);
  }

  // Same file move
  {
    const char *f = "/tmp/mv_same.txt";
    helper_write_file(f, "content");
    char *argv[] = {"mv", (char *)f, (char *)f, NULL};
    int ret = mv_main(3, argv);
    report_test(ctx, "mv: same file move returns error", ret != 0);
    unlink(f);
  }

  // Single file move (rename)
  {
    const char *src = "/tmp/mv_src.txt";
    const char *dst = "/tmp/mv_dst.txt";
    const char *payload = "Movable payload 777";
    helper_write_file(src, payload);

    char *argv[] = {"mv", (char *)src, (char *)dst, NULL};
    int ret = mv_main(3, argv);
    report_test(ctx, "mv: single file move returns 0", ret == 0);
    report_test(ctx, "mv: target file exists", helper_path_exists(dst));
    report_test(ctx, "mv: target content matches payload", helper_read_matches(dst, payload));
    report_test(ctx, "mv: source file was removed", !helper_path_exists(src));

    unlink(dst);
  }

  // Move file into existing directory
  {
    const char *src = "/tmp/mv_f2d.txt";
    const char *dir = "/tmp/mv_tdir";
    const char *expected = "/tmp/mv_tdir/mv_f2d.txt";
    const char *payload = "Moving into dir";
    helper_write_file(src, payload);
    mkdir(dir, 0755);

    char *argv[] = {"mv", (char *)src, (char *)dir, NULL};
    int ret = mv_main(3, argv);
    report_test(ctx, "mv: move file into directory returns 0", ret == 0);
    report_test(ctx, "mv: file exists inside target directory", helper_path_exists(expected));
    report_test(ctx, "mv: content preserved in target directory", helper_read_matches(expected, payload));
    report_test(ctx, "mv: original file location unlinked", !helper_path_exists(src));

    unlink(expected);
    rmdir(dir);
  }

  // Move directory
  {
    const char *src_dir = "/tmp/mv_dsrc";
    const char *dst_dir = "/tmp/mv_ddst";
    const char *inner_file = "/tmp/mv_dsrc/child.txt";
    const char *expected_inner = "/tmp/mv_ddst/child.txt";
    mkdir(src_dir, 0755);
    helper_write_file(inner_file, "child content");

    char *argv[] = {"mv", (char *)src_dir, (char *)dst_dir, NULL};
    int ret = mv_main(3, argv);
    report_test(ctx, "mv: directory move returns 0", ret == 0);
    report_test(ctx, "mv: destination directory exists", helper_path_exists(dst_dir));
    report_test(ctx, "mv: inner file exists in moved directory", helper_path_exists(expected_inner));
    report_test(ctx, "mv: source directory is gone", !helper_path_exists(src_dir));

    unlink(expected_inner);
    rmdir(dst_dir);
  }

  // Multiple files moved to directory
  {
    const char *f1 = "/tmp/mv_m1.txt";
    const char *f2 = "/tmp/mv_m2.txt";
    const char *dir = "/tmp/mv_mdir";
    helper_write_file(f1, "1");
    helper_write_file(f2, "2");
    mkdir(dir, 0755);

    char *argv[] = {"mv", (char *)f1, (char *)f2, (char *)dir, NULL};
    int ret = mv_main(4, argv);
    report_test(ctx, "mv: multiple files move returns 0", ret == 0);
    report_test(ctx, "mv: f1 exists in target dir", helper_path_exists("/tmp/mv_mdir/mv_m1.txt"));
    report_test(ctx, "mv: f2 exists in target dir", helper_path_exists("/tmp/mv_mdir/mv_m2.txt"));
    report_test(ctx, "mv: original f1 removed", !helper_path_exists(f1));
    report_test(ctx, "mv: original f2 removed", !helper_path_exists(f2));

    unlink("/tmp/mv_mdir/mv_m1.txt");
    unlink("/tmp/mv_mdir/mv_m2.txt");
    rmdir(dir);
  }
}

// ── 5. Tree Tests ──────────────────────────────────────────────

static void test_tree_suite(test_ctx_t *ctx) {
  printf("--- tree Tests ---\n");

  // tree on /proc must complete cleanly and not recurse into . or ..
  {
    char *argv[] = {"tree", "/proc", NULL};
    int ret = tree_main(2, argv);
    report_test(ctx, "tree: /proc terminates cleanly without infinite recursion", ret == 0);
  }

  // tree on structured directory hierarchy
  {
    const char *base = "/tmp/tree_suite";
    const char *sub = "/tmp/tree_suite/nested";
    const char *f1 = "/tmp/tree_suite/file1.txt";
    const char *f2 = "/tmp/tree_suite/nested/file2.txt";

    mkdir(base, 0755);
    mkdir(sub, 0755);
    helper_write_file(f1, "hello");
    helper_write_file(f2, "world");

    char *argv[] = {"tree", (char *)base, NULL};
    int ret = tree_main(2, argv);
    report_test(ctx, "tree: nested directory tree returns 0", ret == 0);

    unlink(f2);
    unlink(f1);
    rmdir(sub);
    rmdir(base);
  }

  // tree on empty directory
  {
    const char *empty_dir = "/tmp/tree_empty";
    mkdir(empty_dir, 0755);

    char *argv[] = {"tree", (char *)empty_dir, NULL};
    int ret = tree_main(2, argv);
    report_test(ctx, "tree: empty directory returns 0", ret == 0);

    rmdir(empty_dir);
  }

  // tree on non-existent directory
  {
    char *argv[] = {"tree", "/tmp/nonexistent_tree_path", NULL};
    int ret = tree_main(2, argv);
    report_test(ctx, "tree: non-existent path handles cleanly", ret == 0);
  }
}

// ── 6. Hard Link & Symlink Tests ─────────────────────────────────

static void test_links_suite(test_ctx_t *ctx) {
  printf("--- Link & Symlink Tests ---\n");

  // Hard link test: shared data and nlink count
  {
    const char *orig = "/tmp/hl_orig.txt";
    const char *link_dst = "/tmp/hl_link.txt";
    unlink(orig);
    unlink(link_dst);

    bool w = helper_write_file(orig, "shared_hardlink_data");
    report_test(ctx, "ln: write original file", w);

    char *argv_ln[] = {"ln", (char *)orig, (char *)link_dst, NULL};
    int ret_ln = ln_main(3, argv_ln);
    report_test(ctx, "ln: create hard link", ret_ln == 0);

    struct stat st1, st2;
    int s1 = stat(orig, &st1);
    int s2 = stat(link_dst, &st2);
    report_test(ctx, "ln: stat original has nlink == 2", s1 == 0 && st1.st_nlink == 2);
    report_test(ctx, "ln: stat link has nlink == 2", s2 == 0 && st2.st_nlink == 2);
    report_test(ctx, "ln: link content matches original", helper_read_matches(link_dst, "shared_hardlink_data"));

    // Modify original and verify link sees changes
    helper_write_file(orig, "modified_hardlink_data");
    report_test(ctx, "ln: modified content reflects on link", helper_read_matches(link_dst, "modified_hardlink_data"));

    // Unlink original and verify link persists
    unlink(orig);
    report_test(ctx, "ln: link persists after unlinking original", helper_read_matches(link_dst, "modified_hardlink_data"));
    report_test(ctx, "ln: link nlink decrements to 1", stat(link_dst, &st2) == 0 && st2.st_nlink == 1);

    unlink(link_dst);
  }

  // Symlink test: lstat, stat, readlink, cat through link
  {
    const char *target = "/tmp/sl_target.txt";
    const char *slink = "/tmp/sl_link.txt";
    unlink(target);
    unlink(slink);

    bool w = helper_write_file(target, "symlink_payload_777");
    report_test(ctx, "ln -s: write target file", w);

    char *argv_sl[] = {"ln", "-s", (char *)target, (char *)slink, NULL};
    int ret_sl = ln_main(4, argv_sl);
    report_test(ctx, "ln -s: create symbolic link", ret_sl == 0);

    struct stat st_lstat, st_stat;
    int r_lstat = lstat(slink, &st_lstat);
    int r_stat = stat(slink, &st_stat);
    report_test(ctx, "ln -s: lstat detects S_IFLNK", r_lstat == 0 && S_ISLNK(st_lstat.st_mode));
    report_test(ctx, "ln -s: stat resolves target S_IFREG", r_stat == 0 && S_ISREG(st_stat.st_mode));

    // readlink test via C API
    char rlink_buf[128];
    ssize_t rlen = readlink(slink, rlink_buf, sizeof(rlink_buf) - 1);
    if (rlen >= 0) rlink_buf[rlen] = '\0';
    report_test(ctx, "readlink: returns correct target path", rlen > 0 && strcmp(rlink_buf, target) == 0);

    // readlink test via ebin CLI
    char *argv_rl[] = {"readlink", (char *)slink, NULL};
    int ret_rl = readlink_main(2, argv_rl);
    report_test(ctx, "readlink: CLI invocation returns 0", ret_rl == 0);

    // Reading file through symlink
    report_test(ctx, "ln -s: reading through symlink delivers payload", helper_read_matches(slink, "symlink_payload_777"));

    unlink(slink);
    unlink(target);
  }

  // Symlink loop test: cycle detection ELOOP
  {
    const char *loopA = "/tmp/loopA";
    const char *loopB = "/tmp/loopB";
    unlink(loopA);
    unlink(loopB);

    symlink(loopB, loopA);
    symlink(loopA, loopB);

    struct stat st;
    int ret = stat(loopA, &st);
    report_test(ctx, "symlink: circular loop triggers ELOOP", ret != 0 && errno == ELOOP);

    unlink(loopA);
    unlink(loopB);
  }
}

// ── Entry Point ────────────────────────────────────────────────

int test_coreutils_main(int argc, char **argv) {
  printf("\n=== Running Coreutils (rm, cp, mv, sleep, tree, ln, readlink) Unit Tests ===\n");
  test_ctx_t ctx = {0, 0};

  test_sleep_suite(&ctx);
  test_rm_suite(&ctx);
  test_cp_suite(&ctx);
  test_mv_suite(&ctx);
  test_tree_suite(&ctx);
  test_links_suite(&ctx);

  printf("\nCoreutils Tests: %d passed, %d failed\n\n", ctx.passed, ctx.failed);
  return (ctx.failed == 0) ? 0 : 1;
}

EOS_BIN_ATTR const eos_bin_t test_coreutils_bin = {
    EOS_BIN_INITIALIZER,
    .filename = "test_coreutils",
    .name = "Coreutils Unit Tests",
    .group = "Tests",
    .description = "Unit test suite for rm, cp, mv, sleep, and tree utilities",
    .entry_point = test_coreutils_main,
};
