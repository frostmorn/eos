#include "ebin/cpubench/cpubench.h"
#include "ecore/bin.h"
#include "emisc/eos_hash.h"

#include <esp_heap_caps.h>
#include <esp_timer.h>

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
  const char *flag;
  uint32_t mask;
  const char *desc;
} cpubench_opt_t;

static const cpubench_opt_t cpubench_opts[] = {
    {"--all", CPUBENCH_MASK_ALL, "Run all CPU & memory microbenchmarks (default)"},
    {"--int32", CPUBENCH_MASK_INT32, "INT32 multiply-add throughput (MOPS)"},
    {"--fp32", CPUBENCH_MASK_FP32, "FP32 multiply-add throughput (MFLOPS)"},
    {"--fp64", CPUBENCH_MASK_FP64, "FP64 multiply-add throughput (MFLOPS)"},
    {"--memcpy", CPUBENCH_MASK_MEMCPY, "Small-block DRAM memcpy (32/64/128 B)"},
    {"--rand", CPUBENCH_MASK_RAND, "Random RAM read latency (DRAM 16KB / PSRAM 1MB)"},
    {"--fft", CPUBENCH_MASK_FFT, "1024-point radix-2 FP32 FFT"},
    {"--matmul", CPUBENCH_MASK_MATMUL, "32x32 dense FP32 matrix multiplication"},
    {"--sha256", CPUBENCH_MASK_SHA256, "In-RAM SHA-256 throughput (16KB buffer)"},
};

static double bench_int32_mops(void) {
  volatile int32_t sink = 0;
  int32_t a0 = CPUBENCH_INT32_SEED_A;
  int32_t a1 = CPUBENCH_INT32_SEED_A;
  int32_t a2 = CPUBENCH_INT32_SEED_A;
  int32_t a3 = CPUBENCH_INT32_SEED_A;
  const int32_t b = CPUBENCH_INT32_SEED_B;
  const int32_t c = CPUBENCH_INT32_SEED_C;

  uint64_t ops = 0;
  int64_t t0 = esp_timer_get_time();
  int64_t now = t0;
  while ((now - t0) < CPUBENCH_DURATION_US) {
    for (uint32_t i = 0; i < CPUBENCH_INNER_ITERS; i++) {
      a0 = a0 * b + c;
      a1 = a1 * b + c;
      a2 = a2 * b + c;
      a3 = a3 * b + c;
    }
    ops += (uint64_t)CPUBENCH_INNER_ITERS * CPUBENCH_UNROLL * CPUBENCH_OPS_PER_FMA;
    now = esp_timer_get_time();
  }
  sink = a0 + a1 + a2 + a3;
  (void)sink;
  double secs = (double)(now - t0) / CPUBENCH_USEC_PER_SEC;
  return (secs > 0.0) ? ((double)ops / secs / CPUBENCH_MEGA) : 0.0;
}

static double bench_fp32_mflops(void) {
  volatile float sink = 0.0f;
  float a0 = CPUBENCH_FP32_SEED_A;
  float a1 = CPUBENCH_FP32_SEED_A;
  float a2 = CPUBENCH_FP32_SEED_A;
  float a3 = CPUBENCH_FP32_SEED_A;
  const float b = CPUBENCH_FP32_SEED_B;
  const float c = CPUBENCH_FP32_SEED_C;

  uint64_t ops = 0;
  int64_t t0 = esp_timer_get_time();
  int64_t now = t0;
  while ((now - t0) < CPUBENCH_DURATION_US) {
    for (uint32_t i = 0; i < CPUBENCH_INNER_ITERS; i++) {
      a0 = a0 * b + c;
      a1 = a1 * b + c;
      a2 = a2 * b + c;
      a3 = a3 * b + c;
    }
    ops += (uint64_t)CPUBENCH_INNER_ITERS * CPUBENCH_UNROLL * CPUBENCH_OPS_PER_FMA;
    now = esp_timer_get_time();
  }
  sink = a0 + a1 + a2 + a3;
  (void)sink;
  double secs = (double)(now - t0) / CPUBENCH_USEC_PER_SEC;
  return (secs > 0.0) ? ((double)ops / secs / CPUBENCH_MEGA) : 0.0;
}

static double bench_fp64_mflops(void) {
  volatile double sink = 0.0;
  double a0 = CPUBENCH_FP64_SEED_A;
  double a1 = CPUBENCH_FP64_SEED_A;
  double a2 = CPUBENCH_FP64_SEED_A;
  double a3 = CPUBENCH_FP64_SEED_A;
  const double b = CPUBENCH_FP64_SEED_B;
  const double c = CPUBENCH_FP64_SEED_C;

  uint64_t ops = 0;
  int64_t t0 = esp_timer_get_time();
  int64_t now = t0;
  while ((now - t0) < CPUBENCH_DURATION_US) {
    for (uint32_t i = 0; i < CPUBENCH_INNER_ITERS; i++) {
      a0 = a0 * b + c;
      a1 = a1 * b + c;
      a2 = a2 * b + c;
      a3 = a3 * b + c;
    }
    ops += (uint64_t)CPUBENCH_INNER_ITERS * CPUBENCH_UNROLL * CPUBENCH_OPS_PER_FMA;
    now = esp_timer_get_time();
  }
  sink = a0 + a1 + a2 + a3;
  (void)sink;
  double secs = (double)(now - t0) / CPUBENCH_USEC_PER_SEC;
  return (secs > 0.0) ? ((double)ops / secs / CPUBENCH_MEGA) : 0.0;
}

static void bench_memcpy_row(size_t sz) {
  uint8_t *src = (uint8_t *)heap_caps_malloc(sz, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  uint8_t *dst = (uint8_t *)heap_caps_malloc(sz, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!src || !dst) {
    heap_caps_free(src);
    heap_caps_free(dst);
    return;
  }
  memset(src, CPUBENCH_FILL_BYTE, sz);
  uint64_t calls = 0;
  int64_t t0 = esp_timer_get_time();
  int64_t now = t0;
  while ((now - t0) < CPUBENCH_DURATION_US) {
    for (uint32_t i = 0; i < CPUBENCH_MEMCPY_ITERS; i++) {
      memcpy(dst, src, sz);
    }
    calls += CPUBENCH_MEMCPY_ITERS;
    now = esp_timer_get_time();
  }
  volatile uint8_t sink = dst[0];
  (void)sink;
  heap_caps_free(src);
  heap_caps_free(dst);
  double secs = (double)(now - t0) / CPUBENCH_USEC_PER_SEC;
  double mbps = (secs > 0.0) ? (((double)calls * (double)sz) / secs / CPUBENCH_MIB) : 0.0;
  double ns = (calls > 0U) ? (secs * CPUBENCH_NSEC_PER_SEC / (double)calls) : 0.0;
  printf("| %5u B  | %12.2f | %12.2f |\n", (unsigned)sz, mbps, ns);
}

static void bench_rand_row(const char *label, size_t bytes, uint32_t caps) {
  size_t count = bytes / sizeof(uint32_t);
  uint32_t *arr = (uint32_t *)heap_caps_malloc(bytes, caps);
  if (!arr || count == 0U) {
    heap_caps_free(arr);
    return;
  }
  uint32_t rng = CPUBENCH_INT32_SEED_A;
  for (size_t i = 0; i < count; i++) {
    rng = rng * CPUBENCH_LCG_MUL + CPUBENCH_LCG_INC;
    arr[i] = (uint32_t)(rng % count);
  }
  volatile uint32_t sink = 0;
  uint32_t idx = 0;
  uint64_t reads = 0;
  int64_t t0 = esp_timer_get_time();
  int64_t now = t0;
  while ((now - t0) < CPUBENCH_DURATION_US) {
    for (uint32_t i = 0; i < CPUBENCH_RAND_ITERS; i++) {
      idx = arr[idx];
    }
    reads += CPUBENCH_RAND_ITERS;
    now = esp_timer_get_time();
  }
  sink = idx;
  (void)sink;
  heap_caps_free(arr);
  double secs = (double)(now - t0) / CPUBENCH_USEC_PER_SEC;
  double ns = (reads > 0U) ? (secs * CPUBENCH_NSEC_PER_SEC / (double)reads) : 0.0;
  double mbps = (secs > 0.0) ? (((double)reads * (double)sizeof(uint32_t)) / secs / CPUBENCH_MIB) : 0.0;
  printf("| %-8s | %12.2f | %12.2f |\n", label, ns, mbps);
}

static void fft_bit_reverse(float *re, float *im, uint32_t n) {
  uint32_t j = 0;
  for (uint32_t i = 0; i < n; i++) {
    if (i < j) {
      float tr = re[i];
      float ti = im[i];
      re[i] = re[j];
      im[i] = im[j];
      re[j] = tr;
      im[j] = ti;
    }
    uint32_t bit = n >> 1U;
    while (j & bit) {
      j ^= bit;
      bit >>= 1U;
    }
    j |= bit;
  }
}

static void fft_radix2(float *re, float *im, uint32_t n) {
  fft_bit_reverse(re, im, n);
  for (uint32_t len = 2U; len <= n; len <<= 1U) {
    float ang = -CPUBENCH_FFT_TWO_PI / (float)len;
    float wlen_r = cosf(ang);
    float wlen_i = sinf(ang);
    uint32_t half = len >> 1U;
    for (uint32_t i = 0; i < n; i += len) {
      float wr = 1.0f;
      float wi = 0.0f;
      for (uint32_t j = 0; j < half; j++) {
        uint32_t u_idx = i + j;
        uint32_t v_idx = u_idx + half;
        float vr = re[v_idx] * wr - im[v_idx] * wi;
        float vi = re[v_idx] * wi + im[v_idx] * wr;
        re[v_idx] = re[u_idx] - vr;
        im[v_idx] = im[u_idx] - vi;
        re[u_idx] += vr;
        im[u_idx] += vi;
        float nwr = wr * wlen_r - wi * wlen_i;
        wi = wr * wlen_i + wi * wlen_r;
        wr = nwr;
      }
    }
  }
}

static void bench_fft_1024(double *out_us, double *out_mflops) {
  float *re = (float *)malloc(CPUBENCH_FFT_N * sizeof(float));
  float *im = (float *)malloc(CPUBENCH_FFT_N * sizeof(float));
  if (!re || !im) {
    free(re);
    free(im);
    *out_us = 0.0;
    *out_mflops = 0.0;
    return;
  }
  uint64_t ffts = 0;
  int64_t t0 = esp_timer_get_time();
  int64_t now = t0;
  while ((now - t0) < CPUBENCH_DURATION_US) {
    for (uint32_t k = 0; k < CPUBENCH_FFT_N; k++) {
      re[k] = (float)(k & (CPUBENCH_FFT_N - 1U)) * CPUBENCH_FFT_HALF_SCALE;
      im[k] = 0.0f;
    }
    fft_radix2(re, im, CPUBENCH_FFT_N);
    ffts++;
    now = esp_timer_get_time();
  }
  volatile float sink = re[0] + im[0];
  (void)sink;
  free(re);
  free(im);
  double secs = (double)(now - t0) / CPUBENCH_USEC_PER_SEC;
  double flops_per_fft =
      CPUBENCH_FFT_FLOP_FACTOR * (double)CPUBENCH_FFT_N * (double)CPUBENCH_FFT_LOG2;
  *out_us = (ffts > 0U) ? ((double)(now - t0) / (double)ffts) : 0.0;
  *out_mflops = (secs > 0.0) ? ((double)ffts * flops_per_fft / secs / CPUBENCH_MEGA) : 0.0;
}

static void bench_matmul_32(double *out_us, double *out_mflops) {
  const size_t n = CPUBENCH_MATMUL_N;
  const size_t elems = n * n;
  float *a = (float *)malloc(elems * sizeof(float));
  float *b = (float *)malloc(elems * sizeof(float));
  float *c = (float *)malloc(elems * sizeof(float));
  if (!a || !b || !c) {
    free(a);
    free(b);
    free(c);
    *out_us = 0.0;
    *out_mflops = 0.0;
    return;
  }
  for (size_t i = 0; i < elems; i++) {
    a[i] = (float)(i + 1U) * CPUBENCH_MATMUL_SEED_SCALE;
    b[i] = (float)((elems - i) + 1U) * CPUBENCH_MATMUL_SEED_SCALE;
  }
  uint64_t runs = 0;
  int64_t t0 = esp_timer_get_time();
  int64_t now = t0;
  while ((now - t0) < CPUBENCH_DURATION_US) {
    for (size_t i = 0; i < n; i++) {
      for (size_t j = 0; j < n; j++) {
        float sum = 0.0f;
        for (size_t k = 0; k < n; k++) {
          sum += a[i * n + k] * b[k * n + j];
        }
        c[i * n + j] = sum;
      }
    }
    runs++;
    now = esp_timer_get_time();
  }
  volatile float sink = c[0];
  (void)sink;
  free(a);
  free(b);
  free(c);
  double secs = (double)(now - t0) / CPUBENCH_USEC_PER_SEC;
  double flops_per_run = CPUBENCH_MATMUL_FLOP_FACTOR * (double)n * (double)n * (double)n;
  *out_us = (runs > 0U) ? ((double)(now - t0) / (double)runs) : 0.0;
  *out_mflops = (secs > 0.0) ? ((double)runs * flops_per_run / secs / CPUBENCH_MEGA) : 0.0;
}

static double bench_sha256_mbps(void) {
  uint8_t *buf = (uint8_t *)heap_caps_malloc(CPUBENCH_SHA256_BUF_BYTES,
                                             MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!buf) {
    return 0.0;
  }
  memset(buf, CPUBENCH_FILL_BYTE, CPUBENCH_SHA256_BUF_BYTES);
  uint8_t digest[CPUBENCH_SHA256_DIGEST_BYTES];
  uint64_t total_bytes = 0;
  int64_t t0 = esp_timer_get_time();
  int64_t now = t0;
  while ((now - t0) < CPUBENCH_DURATION_US) {
    eos_hash_buf(EOS_HASH_SHA256, buf, CPUBENCH_SHA256_BUF_BYTES, digest);
    total_bytes += CPUBENCH_SHA256_BUF_BYTES;
    now = esp_timer_get_time();
  }
  volatile uint8_t sink = digest[0];
  (void)sink;
  heap_caps_free(buf);
  double secs = (double)(now - t0) / CPUBENCH_USEC_PER_SEC;
  return (secs > 0.0) ? ((double)total_bytes / secs / CPUBENCH_MIB) : 0.0;
}

static void print_usage(void) {
  printf("Usage: cpubench [options]\n");
  for (size_t i = 0; i < sizeof(cpubench_opts) / sizeof(cpubench_opts[0]); i++) {
    printf("  %-10s %s\n", cpubench_opts[i].flag, cpubench_opts[i].desc);
  }
  printf("  -h, --help Show this help\n");
}

static int parse_args(int argc, char **argv, uint32_t *out_mask) {
  uint32_t mask = 0;
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
      print_usage();
      return 1;
    }
    bool matched = false;
    for (size_t j = 0; j < sizeof(cpubench_opts) / sizeof(cpubench_opts[0]); j++) {
      if (strcmp(argv[i], cpubench_opts[j].flag) == 0) {
        mask |= cpubench_opts[j].mask;
        matched = true;
        break;
      }
    }
    if (!matched) {
      printf("cpubench: unknown option '%s'\n", argv[i]);
      print_usage();
      return -1;
    }
  }
  *out_mask = (mask == 0U) ? CPUBENCH_MASK_ALL : mask;
  return 0;
}

static void run_compute_section(uint32_t mask) {
  uint32_t compute_mask = CPUBENCH_MASK_INT32 | CPUBENCH_MASK_FP32 | CPUBENCH_MASK_FP64 |
                          CPUBENCH_MASK_FFT | CPUBENCH_MASK_MATMUL | CPUBENCH_MASK_SHA256;
  if ((mask & compute_mask) == 0U) {
    return;
  }
  printf("** CPU & DSP Compute\n\n");
  printf("+--------------------+--------------+--------------+\n");
  printf("| Workload           |   Throughput |   Latency    |\n");
  printf("+--------------------+--------------+--------------+\n");
  if (mask & CPUBENCH_MASK_INT32) {
    printf("| INT32 MAC          | %7.2f MOPS |          N/A |\n", bench_int32_mops());
    usleep(CPUBENCH_COOLDOWN_US);
  }
  if (mask & CPUBENCH_MASK_FP32) {
    printf("| FP32 MAC           | %5.2f MFLOPS |          N/A |\n", bench_fp32_mflops());
    usleep(CPUBENCH_COOLDOWN_US);
  }
  if (mask & CPUBENCH_MASK_FP64) {
    printf("| FP64 MAC           | %5.2f MFLOPS |          N/A |\n", bench_fp64_mflops());
    usleep(CPUBENCH_COOLDOWN_US);
  }
  if (mask & CPUBENCH_MASK_FFT) {
    double fft_us = 0.0;
    double fft_mflops = 0.0;
    bench_fft_1024(&fft_us, &fft_mflops);
    printf("| FFT-1024 (FP32)    | %5.2f MFLOPS | %9.1f us |\n", fft_mflops, fft_us);
    usleep(CPUBENCH_COOLDOWN_US);
  }
  if (mask & CPUBENCH_MASK_MATMUL) {
    double mat_us = 0.0;
    double mat_mflops = 0.0;
    bench_matmul_32(&mat_us, &mat_mflops);
    printf("| MatMul 32x32 (FP32)| %5.2f MFLOPS | %9.1f us |\n", mat_mflops, mat_us);
    usleep(CPUBENCH_COOLDOWN_US);
  }
  if (mask & CPUBENCH_MASK_SHA256) {
    printf("| SHA-256 (16KB RAM) | %7.2f MB/s |          N/A |\n", bench_sha256_mbps());
  }
  printf("+--------------------+--------------+--------------+\n\n");
}

static int cpubench_main(int argc, char **argv) {
  uint32_t mask = 0;
  int rc = parse_args(argc, argv, &mask);
  if (rc != 0) {
    return (rc > 0) ? 0 : 1;
  }

  printf("\n===========================================================\n");
  printf("*                   EOS CPU Benchmark                     *\n");
  printf("===========================================================\n\n");

  run_compute_section(mask);

  if (mask & CPUBENCH_MASK_MEMCPY) {
    printf("** Small Block memcpy (DRAM)\n\n");
    printf("+----------+--------------+--------------+\n");
    printf("|  block   |   copy MB/s  |    ns / op   |\n");
    printf("+----------+--------------+--------------+\n");
    bench_memcpy_row(CPUBENCH_BLK_32);
    bench_memcpy_row(CPUBENCH_BLK_64);
    bench_memcpy_row(CPUBENCH_BLK_128);
    printf("+----------+--------------+--------------+\n\n");
  }

  if (mask & CPUBENCH_MASK_RAND) {
    printf("** Random RAM Read Latency\n\n");
    printf("+----------+--------------+--------------+\n");
    printf("|  target  |   ns / read  |   read MB/s  |\n");
    printf("+----------+--------------+--------------+\n");
    bench_rand_row("DRAM 16K", CPUBENCH_RAND_DRAM_BYTES,
                   MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    bench_rand_row("PSRAM 1M", CPUBENCH_RAND_PSRAM_BYTES,
                   MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    printf("+----------+--------------+--------------+\n\n");
  }

  printf("Benchmark complete.\n\n");
  return 0;
}

EOS_BIN_ATTR const eos_bin_t cpubench_app = {
    EOS_BIN_INITIALIZER,
    .filename = "cpubench",
    .name = "CPU Benchmark",
    .group = "Tools",
    .description = "Benchmark INT32, FP32/FP64 FLOPS, FFT, matmul, SHA-256, memcpy, and RAM latency",
    .entry_point = cpubench_main,
};
