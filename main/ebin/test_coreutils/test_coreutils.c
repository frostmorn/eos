#include "ecore/bin.h"
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

// Forward declarations of ebin entry points
extern int sleep_main(int argc, char **argv);
extern int rm_main(int argc, char **argv);
extern int cp_main(int argc, char **argv);
extern int mv_main(int argc, char **argv);

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

// ── Entry Point ────────────────────────────────────────────────

int test_coreutils_main(int argc, char **argv) {
  printf("\n=== Running Coreutils (rm, cp, mv, sleep) Unit Tests ===\n");
  test_ctx_t ctx = {0, 0};

  test_sleep_suite(&ctx);
  test_rm_suite(&ctx);
  test_cp_suite(&ctx);
  test_mv_suite(&ctx);

  printf("\nCoreutils Tests: %d passed, %d failed\n\n", ctx.passed, ctx.failed);
  return (ctx.failed == 0) ? 0 : 1;
}

EOS_BIN_ATTR const eos_bin_t test_coreutils_bin = {
    EOS_BIN_INITIALIZER,
    .filename = "test_coreutils",
    .name = "Coreutils Unit Tests",
    .group = "Tests",
    .description = "Unit test suite for rm, cp, mv, and sleep utilities",
    .entry_point = test_coreutils_main,
};
