#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <unistd.h>

#include "ecore/bin.h"
#include "ecore/ioctl.h"
#include "ebin/test_sound/test_sound.h"
#include "emisc/fancymacro.h"
#include "emisc/strlimits.h"

typedef struct {
  int freq;
  int dur_ms;
  int gap_ms;
} imperial_note_t;

// Star Wars: The Imperial March (Darth Vader's Theme) in G minor (~104 BPM standard tempo)
static const imperial_note_t imperial_march[] = {
    // Theme Bar 1
    {392, 550, 50},   // G4
    {392, 550, 50},   // G4
    {392, 550, 50},   // G4
    {311, 420, 40},   // Eb4
    {466, 180, 30},   // Bb4
    {392, 550, 50},   // G4
    {311, 420, 40},   // Eb4
    {466, 180, 30},   // Bb4
    {392, 1050, 100}, // G4
    // Theme Bar 2
    {587, 550, 50},   // D5
    {587, 550, 50},   // D5
    {587, 550, 50},   // D5
    {622, 420, 40},   // Eb5
    {466, 180, 30},   // Bb4
    {370, 550, 50},   // F#4 / Gb4
    {311, 420, 40},   // Eb4
    {466, 180, 30},   // Bb4
    {392, 1150, 100}, // G4
};

static bool parse_sound_flag(const char *arg, const char *next, sound_opts_t *opts, int *consumed) {
  *consumed = 0;
  if (strcmp(arg, "-v") == 0 || strcmp(arg, "--verbose") == 0 || strcmp(arg, "--timing") == 0) {
    opts->verbose = 1;
    return true;
  }
  if (strcmp(arg, "-m") == 0 || strcmp(arg, "--march") == 0 || strcmp(arg, "--imperial") == 0) {
    opts->is_march = 1;
    return true;
  }
  if (strcmp(arg, "-t") == 0 || strcmp(arg, "--time") == 0 ||
      strcmp(arg, "-d") == 0 || strcmp(arg, "--duration") == 0) {
    if (next && next[0] != '-') {
      opts->duration_ms = atoi(next);
      if (opts->duration_ms < 0) opts->duration_ms = 0;
      *consumed = 1;
    }
    return true;
  }
  if (strncmp(arg, "--time=", 7) == 0) {
    opts->duration_ms = atoi(arg + 7);
    if (opts->duration_ms < 0) opts->duration_ms = 0;
    return true;
  }
  if (strncmp(arg, "-t=", 3) == 0) {
    opts->duration_ms = atoi(arg + 3);
    if (opts->duration_ms < 0) opts->duration_ms = 0;
    return true;
  }
  return false;
}

static void parse_sound_pos_arg(int pos_arg, const char *arg, sound_opts_t *opts) {
  if (pos_arg == 1) {
    if (strcasecmp(arg, "march") == 0 || strcasecmp(arg, "imperial") == 0) {
      opts->is_march = 1;
    } else {
      opts->freq = atoi(arg);
      if (opts->freq <= 0) opts->freq = 440;
    }
  } else if (pos_arg == 2) {
    if (opts->duration_ms < 0) {
      opts->duration_ms = atoi(arg);
      if (opts->duration_ms <= 0) opts->duration_ms = EOS_MS_PER_SEC;
    }
  } else if (pos_arg == 3) {
    opts->volume = atoi(arg);
    if (opts->volume < 0) opts->volume = 0;
    if (opts->volume > 100) opts->volume = 100;
  } else if (pos_arg == 4) {
    if (arg[0] == '/') {
      opts->path = arg;
    } else {
      opts->mult = atoi(arg);
      if (opts->mult < 0) opts->mult = 0;
      if (opts->mult > 100) opts->mult = 100;
    }
  } else if (pos_arg == 5) {
    opts->path = arg;
  }
}

static int parse_sound_args(int argc, char **argv, sound_opts_t *opts) {
  opts->freq = 440;
  opts->duration_ms = -1;
  opts->volume = -1;
  opts->mult = -1;
  opts->verbose = 0;
  opts->is_march = 0;
  opts->path = "/dev/sound0";

  int pos_arg = 0;
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
      printf("Usage: test_sound [freq_hz] [duration_ms] [volume 0-100] [mult 0-100] [flags] [path]\n");
      printf("       test_sound march [duration_ms] [volume 0-100] [flags]\n\n");
      printf("Options:\n");
      printf("  -t, --time <ms>       Set playback duration in milliseconds\n");
      printf("  -d, --duration <ms>   Alias for -t\n");
      printf("  -m, --march           Play Star Wars Imperial March theme :D\n");
      printf("  -v, --verbose         Show detailed per-chunk time shift logs\n");
      printf("  --timing              Alias for -v\n");
      return 1;
    }
    int consumed = 0;
    const char *next = (i + 1 < argc) ? argv[i + 1] : NULL;
    if (parse_sound_flag(argv[i], next, opts, &consumed)) {
      i += consumed;
      continue;
    }
    pos_arg++;
    parse_sound_pos_arg(pos_arg, argv[i], opts);
  }

  if (opts->duration_ms < 0) {
    opts->duration_ms = opts->is_march ? EOS_EXTENDED_TIMEOUT_MS : EOS_MS_PER_SEC;
  }
  return 0;
}

static void sound_play_march(int fd, const sound_opts_t *opts, uint32_t sample_rate,
                             uint32_t channels, int16_t *buffer, size_t buffer_frames,
                             sound_perf_stats_t *perf, size_t *out_frames) {
  printf("test_sound: ♫ Playing Star Wars Imperial March ♫ (time=%d ms, vol=%d, mult=%d%%)...\n",
         opts->duration_ms, opts->volume, opts->mult);

  size_t num_notes = sizeof(imperial_march) / sizeof(imperial_march[0]);
  int64_t elapsed_audio_ms = 0;
  size_t frames_rendered = 0;

  for (size_t n = 0; n < num_notes; n++) {
    int note_freq = imperial_march[n].freq;
    int note_dur = imperial_march[n].dur_ms;
    int note_gap = imperial_march[n].gap_ms;

    if (opts->duration_ms > 0 && elapsed_audio_ms + note_dur > opts->duration_ms) {
      note_dur = opts->duration_ms - elapsed_audio_ms;
      if (note_dur <= 0)
        break;
    }

    size_t note_samples = (size_t)((uint64_t)sample_rate * note_dur / (uint64_t)EOS_MS_PER_SEC);
    size_t note_fade = (size_t)(sample_rate * 8 / (size_t)EOS_MS_PER_SEC);
    if (note_fade == 0) note_fade = 1;
    if (note_fade > note_samples / 2) note_fade = note_samples / 2;

    float note_phase = 0.0f;
    float note_step = (float)(2.0 * M_PI * note_freq / sample_rate);
    size_t note_rendered = 0;

    while (note_rendered < note_samples) {
      size_t chunk = note_samples - note_rendered;
      if (chunk > buffer_frames) chunk = buffer_frames;

      struct timeval tv_r0, tv_r1, tv_w1;
      gettimeofday(&tv_r0, NULL);

      for (size_t i = 0; i < chunk; i++) {
        size_t s_idx = note_rendered + i;
        float gain = 1.0f;
        if (s_idx < note_fade) {
          gain = (float)s_idx / (float)note_fade;
        } else if (s_idx >= note_samples - note_fade) {
          gain = (float)(note_samples - s_idx) / (float)note_fade;
        }
        int16_t val = (int16_t)(sinf(note_phase) * (16000.0f * gain));
        note_phase += note_step;
        if (note_phase >= (float)(2.0 * M_PI))
          note_phase -= (float)(2.0 * M_PI);

        for (uint32_t ch = 0; ch < channels; ch++) {
          buffer[i * channels + ch] = val;
        }
      }

      gettimeofday(&tv_r1, NULL);
      int64_t r_us = (int64_t)(tv_r1.tv_sec - tv_r0.tv_sec) * (int64_t)EOS_US_PER_SEC + (tv_r1.tv_usec - tv_r0.tv_usec);
      perf->total_render_us += r_us;
      if (r_us < perf->min_render_us) perf->min_render_us = r_us;
      if (r_us > perf->max_render_us) perf->max_render_us = r_us;

      size_t bytes = chunk * channels * sizeof(int16_t);
      ssize_t written = write(fd, buffer, bytes);
      if (written < 0) break;

      gettimeofday(&tv_w1, NULL);
      int64_t w_us = (int64_t)(tv_w1.tv_sec - tv_r1.tv_sec) * (int64_t)EOS_US_PER_SEC + (tv_w1.tv_usec - tv_r1.tv_usec);
      perf->total_write_us += w_us;
      if (w_us < perf->min_write_us) perf->min_write_us = w_us;
      if (w_us > perf->max_write_us) perf->max_write_us = w_us;
      perf->write_count++;

      note_rendered += chunk;
      frames_rendered += chunk;
    }

    elapsed_audio_ms += note_dur;
    if (opts->duration_ms > 0 && elapsed_audio_ms >= opts->duration_ms)
      break;

    if (note_gap > 0) {
      size_t gap_samples = (size_t)((uint64_t)sample_rate * note_gap / (uint64_t)EOS_MS_PER_SEC);
      memset(buffer, 0, buffer_frames * channels * sizeof(int16_t));
      while (gap_samples > 0) {
        size_t chunk = (gap_samples > buffer_frames) ? buffer_frames : gap_samples;
        size_t bytes = chunk * channels * sizeof(int16_t);
        if (write(fd, buffer, bytes) < 0) break;
        gap_samples -= chunk;
        frames_rendered += chunk;
      }
      elapsed_audio_ms += note_gap;
      if (opts->duration_ms > 0 && elapsed_audio_ms >= opts->duration_ms)
        break;
    }
  }
  *out_frames = frames_rendered;
}

static int sound_play_tone(int fd, const sound_opts_t *opts, uint32_t sample_rate,
                           uint32_t channels, int16_t *buffer, size_t buffer_frames,
                           sound_perf_stats_t *perf, size_t *out_frames) {
  size_t total_samples = (size_t)((uint64_t)sample_rate * opts->duration_ms / (uint64_t)EOS_MS_PER_SEC);
  printf("test_sound: playing %d Hz tone for %d ms (total %zu samples)...\n",
         opts->freq, opts->duration_ms, total_samples);

  float phase = 0.0f;
  float phase_step = (float)(2.0 * M_PI * opts->freq / sample_rate);

  size_t fade_frames = (size_t)(sample_rate * 10 / (size_t)EOS_MS_PER_SEC);
  if (fade_frames == 0) fade_frames = 1;
  if (fade_frames > total_samples / 2) fade_frames = total_samples / 2;

  size_t frames_rendered = 0;
  while (frames_rendered < total_samples) {
    size_t chunk_frames = total_samples - frames_rendered;
    if (chunk_frames > buffer_frames) chunk_frames = buffer_frames;

    struct timeval tv_r0, tv_r1, tv_w1;
    gettimeofday(&tv_r0, NULL);

    for (size_t i = 0; i < chunk_frames; i++) {
      size_t sample_idx = frames_rendered + i;
      float gain = 1.0f;
      if (sample_idx < fade_frames) {
        gain = (float)sample_idx / (float)fade_frames;
      } else if (sample_idx >= total_samples - fade_frames) {
        gain = (float)(total_samples - sample_idx) / (float)fade_frames;
      }

      int16_t val = (int16_t)(sinf(phase) * (16000.0f * gain));
      phase += phase_step;
      if (phase >= (float)(2.0 * M_PI)) phase -= (float)(2.0 * M_PI);

      for (uint32_t ch = 0; ch < channels; ch++) {
        buffer[i * channels + ch] = val;
      }
    }

    gettimeofday(&tv_r1, NULL);
    int64_t r_us = (int64_t)(tv_r1.tv_sec - tv_r0.tv_sec) * (int64_t)EOS_US_PER_SEC + (tv_r1.tv_usec - tv_r0.tv_usec);
    perf->total_render_us += r_us;
    if (r_us < perf->min_render_us) perf->min_render_us = r_us;
    if (r_us > perf->max_render_us) perf->max_render_us = r_us;

    size_t bytes_to_write = chunk_frames * channels * sizeof(int16_t);
    ssize_t written = write(fd, buffer, bytes_to_write);
    if (written < 0) {
      printf("test_sound: write error: %d (%s)\n", errno, strerror(errno));
      return -1;
    }

    gettimeofday(&tv_w1, NULL);
    int64_t w_us = (int64_t)(tv_w1.tv_sec - tv_r1.tv_sec) * (int64_t)EOS_US_PER_SEC + (tv_w1.tv_usec - tv_r1.tv_usec);
    perf->total_write_us += w_us;
    if (w_us < perf->min_write_us) perf->min_write_us = w_us;
    if (w_us > perf->max_write_us) perf->max_write_us = w_us;
    perf->write_count++;

    frames_rendered += chunk_frames;

    int64_t now_us = (int64_t)tv_w1.tv_sec * (int64_t)EOS_US_PER_SEC + tv_w1.tv_usec;
    if (opts->verbose && (now_us - perf->last_checkpoint_us >= (int64_t)(10 * EOS_DEFAULT_POLL_TIMEOUT_US) || frames_rendered >= total_samples)) {
      int64_t wall_ms = (now_us - perf->t0_us) / EOS_MS_PER_SEC;
      int64_t audio_ms = (int64_t)frames_rendered * (int64_t)EOS_MS_PER_SEC / sample_rate;
      int64_t lead_ms = audio_ms - wall_ms;
      printf("test_sound: [%4lld ms wall | %4lld ms audio] lead=%+3lld ms, render=%4lld us, write=%4lld us\n",
             wall_ms, audio_ms, lead_ms, r_us, w_us);
      perf->last_checkpoint_us = now_us;
    }
  }
  *out_frames = frames_rendered;
  return 0;
}

static void sound_print_report(const sound_opts_t *opts, uint32_t sample_rate,
                               uint32_t channels, size_t frames_rendered,
                               const sound_perf_stats_t *perf, int64_t total_wall_us,
                               int have_stats, const eos_sound_stats_t *stats) {
  int64_t audio_dur_us = (int64_t)frames_rendered * (int64_t)EOS_US_PER_SEC / sample_rate;
  int64_t final_lead_us = audio_dur_us - total_wall_us;
  double wall_ms = (double)total_wall_us / (double)EOS_US_PER_MS;
  double render_ms = (double)perf->total_render_us / (double)EOS_US_PER_MS;
  double write_ms = (double)perf->total_write_us / (double)EOS_US_PER_MS;

  if (opts->verbose) {
    printf("test_sound: ── Timing & Time Shift Analysis ───────────────────\n");
    printf("test_sound: target duration:     %d ms (%zu samples @ %lu Hz, %u ch)\n",
           opts->duration_ms, frames_rendered, (unsigned long)sample_rate, (unsigned)channels);
    printf("test_sound: wall clock elapsed:  %.2f ms\n", wall_ms);
    printf("test_sound: synthesis (CPU):     %.2f ms (%.1f%% CPU, avg=%.1f us/chunk)\n",
           render_ms, (wall_ms > 0) ? (render_ms * 100.0 / wall_ms) : 0.0,
           perf->write_count ? ((double)perf->total_render_us / perf->write_count) : 0.0);
    printf("test_sound: write blocking:      %.2f ms (%.1f%% blocked in DMA)\n",
           write_ms, (wall_ms > 0) ? (write_ms * 100.0 / wall_ms) : 0.0);
    printf("test_sound: write chunks:        %zu (min=%.2f ms, avg=%.2f ms, max=%.2f ms)\n",
           perf->write_count, (double)perf->min_write_us / (double)EOS_US_PER_MS,
           perf->write_count ? ((double)perf->total_write_us / ((double)perf->write_count * (double)EOS_US_PER_MS)) : 0.0,
           (double)perf->max_write_us / (double)EOS_US_PER_MS);
    printf("test_sound: pipeline time shift: %+.2f ms (buffered lead at exit)\n",
           (double)final_lead_us / (double)EOS_US_PER_MS);
    if (have_stats) {
      printf("test_sound: driver metrics:      bclk=%lu Hz, dma=[%lux%lu], underruns=%lu, driver_lead=%+.2f ms\n",
             (unsigned long)stats->bclk_actual_hz, (unsigned long)stats->dma_desc_num,
             (unsigned long)stats->dma_frame_num, (unsigned long)stats->underruns,
             (double)stats->last_lead_us / (double)EOS_US_PER_MS);
      printf("test_sound: driver latency:      lock_total=%.2f ms, shift_math=%.2f ms, i2s_wait=%.2f ms\n",
             (double)stats->total_lock_us / (double)EOS_US_PER_MS,
             (double)stats->total_proc_us / (double)EOS_US_PER_MS,
             (double)stats->total_i2s_wait_us / (double)EOS_US_PER_MS);
    }
    printf("test_sound: ───────────────────────────────────────────────────\n");
  }
}

int test_sound_main(int argc, char **argv) {
  sound_opts_t opts;
  if (parse_sound_args(argc, argv, &opts) != 0) {
    return 0;
  }

  printf("test_sound: opening %s...\n", opts.path);
  int fd = open(opts.path, O_WRONLY);
  if (fd < 0) {
    printf("test_sound: failed to open %s: %d (%s)\n", opts.path, errno, strerror(errno));
    return -1;
  }

  eos_sound_config_t cfg;
  if (ioctl(fd, EOS_SOUND_IOCTL_GET_CONFIG, &cfg) == 0) {
    printf("test_sound: current config: rate=%lu Hz, bits=%u, channels=%u, vol=%u, mult=%u%%, delay=%u ms\n",
           (unsigned long)cfg.sample_rate, cfg.bits_per_sample, cfg.channels,
           cfg.volume, cfg.mult, (unsigned)cfg.delay_ms);
  } else {
    cfg.sample_rate = 22050;
    cfg.channels = 1;
    cfg.mult = 10;
    cfg.delay_ms = 90;
  }

  if (opts.mult >= 0) {
    printf("test_sound: setting power multiplier to %d%%...\n", opts.mult);
    ioctl(fd, EOS_SOUND_IOCTL_SET_MULT, (uint32_t)opts.mult);
  }

  if (opts.volume >= 0) {
    printf("test_sound: setting volume to %d...\n", opts.volume);
    ioctl(fd, EOS_SOUND_IOCTL_SET_VOLUME, (uint32_t)opts.volume);
  }

  uint32_t sample_rate = cfg.sample_rate ? cfg.sample_rate : 22050;
  uint32_t channels = cfg.channels ? cfg.channels : 1;

  int16_t buffer[EOS_LARGE_STR_LEN];
  size_t buffer_frames = EOS_SECTOR_SIZE;
  size_t frames_rendered = 0;

  struct timeval tv_start, tv_end;
  gettimeofday(&tv_start, NULL);
  int64_t t0_us = (int64_t)tv_start.tv_sec * (int64_t)EOS_US_PER_SEC + tv_start.tv_usec;

  sound_perf_stats_t perf = {
      .total_render_us = 0,
      .total_write_us = 0,
      .min_write_us = 999999999LL,
      .max_write_us = 0,
      .min_render_us = 999999999LL,
      .max_render_us = 0,
      .write_count = 0,
      .last_checkpoint_us = t0_us,
      .t0_us = t0_us,
  };

  if (opts.is_march) {
    sound_play_march(fd, &opts, sample_rate, channels, buffer, buffer_frames, &perf, &frames_rendered);
  } else {
    if (sound_play_tone(fd, &opts, sample_rate, channels, buffer, buffer_frames, &perf, &frames_rendered) < 0) {
      close(fd);
      return -1;
    }
  }

  memset(buffer, 0, EOS_SECTOR_SIZE * channels * sizeof(int16_t));
  write(fd, buffer, EOS_SECTOR_SIZE * channels * sizeof(int16_t));

  gettimeofday(&tv_end, NULL);
  int64_t total_wall_us = (int64_t)(tv_end.tv_sec - tv_start.tv_sec) * (int64_t)EOS_US_PER_SEC + (tv_end.tv_usec - tv_start.tv_usec);

  eos_sound_stats_t stats = {0};
  int have_stats = (ioctl(fd, EOS_SOUND_IOCTL_GET_STATS, &stats) == 0);

  close(fd);
  printf("test_sound: playback complete.\n");

  sound_print_report(&opts, sample_rate, channels, frames_rendered, &perf, total_wall_us, have_stats, &stats);
  return 0;
}

EOS_BIN_ATTR eos_bin_t test_sound = {
    EOS_BIN_INITIALIZER,
    .filename = "test_sound",
    .name = "Sound Test",
    .group = "Tests",
    .description = "Play a pure audio tone or Imperial March theme through /dev/sound0",
    .entry_point = test_sound_main};
