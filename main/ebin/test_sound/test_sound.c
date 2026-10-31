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

int test_sound_main(int argc, char **argv) {
  int freq = 440;
  int duration_ms = -1; // -1 = not set by user
  int volume = -1;
  int mult = -1;
  int verbose = 0;
  int is_march = 0;
  const char *path = "/dev/sound0";

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
      return 0;
    }
    if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0 ||
        strcmp(argv[i], "--timing") == 0) {
      verbose = 1;
      continue;
    }
    if (strcmp(argv[i], "-m") == 0 || strcmp(argv[i], "--march") == 0 ||
        strcmp(argv[i], "--imperial") == 0) {
      is_march = 1;
      continue;
    }
    if (strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--time") == 0 ||
        strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--duration") == 0) {
      if (i + 1 < argc && argv[i + 1][0] != '-') {
        duration_ms = atoi(argv[++i]);
        if (duration_ms < 0) duration_ms = 0;
      }
      continue;
    }
    if (strncmp(argv[i], "--time=", 7) == 0) {
      duration_ms = atoi(argv[i] + 7);
      if (duration_ms < 0) duration_ms = 0;
      continue;
    }
    if (strncmp(argv[i], "-t=", 3) == 0) {
      duration_ms = atoi(argv[i] + 3);
      if (duration_ms < 0) duration_ms = 0;
      continue;
    }
    pos_arg++;
    if (pos_arg == 1) {
      if (strcasecmp(argv[i], "march") == 0 || strcasecmp(argv[i], "imperial") == 0) {
        is_march = 1;
      } else {
        freq = atoi(argv[i]);
        if (freq <= 0) freq = 440;
      }
    } else if (pos_arg == 2) {
      if (duration_ms < 0) {
        duration_ms = atoi(argv[i]);
        if (duration_ms <= 0) duration_ms = 1000;
      }
    } else if (pos_arg == 3) {
      volume = atoi(argv[i]);
      if (volume < 0) volume = 0;
      if (volume > 100) volume = 100;
    } else if (pos_arg == 4) {
      if (argv[i][0] == '/') {
        path = argv[i];
      } else {
        mult = atoi(argv[i]);
        if (mult < 0) mult = 0;
        if (mult > 100) mult = 100;
      }
    } else if (pos_arg == 5) {
      path = argv[i];
    }
  }

  if (duration_ms < 0) {
    duration_ms = is_march ? 5000 : 1000;
  }

  printf("test_sound: opening %s...\n", path);
  int fd = open(path, O_WRONLY);
  if (fd < 0) {
    printf("test_sound: failed to open %s: %d (%s)\n", path, errno,
           strerror(errno));
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

  if (mult >= 0) {
    printf("test_sound: setting power multiplier to %d%%...\n", mult);
    ioctl(fd, EOS_SOUND_IOCTL_SET_MULT, (uint32_t)mult);
  }

  if (volume >= 0) {
    printf("test_sound: setting volume to %d...\n", volume);
    ioctl(fd, EOS_SOUND_IOCTL_SET_VOLUME, (uint32_t)volume);
  }

  uint32_t sample_rate = cfg.sample_rate ? cfg.sample_rate : 22050;
  uint32_t channels = cfg.channels ? cfg.channels : 1;

  int16_t buffer[512 * 2]; // 512 frames (mono or stereo)
  size_t buffer_frames = 512;
  size_t frames_rendered = 0;

  struct timeval tv_start, tv_end;
  gettimeofday(&tv_start, NULL);
  int64_t t0_us = (int64_t)tv_start.tv_sec * 1000000LL + tv_start.tv_usec;

  int64_t total_render_us = 0;
  int64_t total_write_us = 0;
  int64_t min_write_us = 999999999LL;
  int64_t max_write_us = 0;
  int64_t min_render_us = 999999999LL;
  int64_t max_render_us = 0;
  size_t write_count = 0;
  int64_t last_checkpoint_us = t0_us;

  if (is_march) {
    printf("test_sound: ♫ Playing Star Wars Imperial March ♫ (time=%d ms, vol=%u, mult=%u%%)...\n",
           duration_ms, (volume >= 0) ? (unsigned)volume : cfg.volume,
           (mult >= 0) ? (unsigned)mult : cfg.mult);

    size_t num_notes = sizeof(imperial_march) / sizeof(imperial_march[0]);
    int64_t elapsed_audio_ms = 0;

    for (size_t n = 0; n < num_notes; n++) {
      int note_freq = imperial_march[n].freq;
      int note_dur = imperial_march[n].dur_ms;
      int note_gap = imperial_march[n].gap_ms;

      if (duration_ms > 0 && elapsed_audio_ms + note_dur > duration_ms) {
        note_dur = duration_ms - elapsed_audio_ms;
        if (note_dur <= 0)
          break;
      }

      size_t note_samples = (size_t)((uint64_t)sample_rate * note_dur / 1000);
      size_t note_fade = (size_t)(sample_rate * 8 / 1000); // 8ms envelope
      if (note_fade == 0)
        note_fade = 1;
      if (note_fade > note_samples / 2)
        note_fade = note_samples / 2;

      float note_phase = 0.0f;
      float note_step = (float)(2.0 * M_PI * note_freq / sample_rate);
      size_t note_rendered = 0;

      while (note_rendered < note_samples) {
        size_t chunk = note_samples - note_rendered;
        if (chunk > buffer_frames)
          chunk = buffer_frames;

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
        int64_t r_us = (int64_t)(tv_r1.tv_sec - tv_r0.tv_sec) * 1000000LL + (tv_r1.tv_usec - tv_r0.tv_usec);
        total_render_us += r_us;
        if (r_us < min_render_us) min_render_us = r_us;
        if (r_us > max_render_us) max_render_us = r_us;

        size_t bytes = chunk * channels * sizeof(int16_t);
        ssize_t written = write(fd, buffer, bytes);
        if (written < 0) break;

        gettimeofday(&tv_w1, NULL);
        int64_t w_us = (int64_t)(tv_w1.tv_sec - tv_r1.tv_sec) * 1000000LL + (tv_w1.tv_usec - tv_r1.tv_usec);
        total_write_us += w_us;
        if (w_us < min_write_us) min_write_us = w_us;
        if (w_us > max_write_us) max_write_us = w_us;
        write_count++;

        note_rendered += chunk;
        frames_rendered += chunk;
      }

      elapsed_audio_ms += note_dur;
      if (duration_ms > 0 && elapsed_audio_ms >= duration_ms)
        break;

      // Inter-note staccato pause
      if (note_gap > 0) {
        size_t gap_samples = (size_t)((uint64_t)sample_rate * note_gap / 1000);
        memset(buffer, 0, sizeof(buffer));
        while (gap_samples > 0) {
          size_t chunk = (gap_samples > buffer_frames) ? buffer_frames : gap_samples;
          size_t bytes = chunk * channels * sizeof(int16_t);
          if (write(fd, buffer, bytes) < 0) break;
          gap_samples -= chunk;
          frames_rendered += chunk;
        }
        elapsed_audio_ms += note_gap;
        if (duration_ms > 0 && elapsed_audio_ms >= duration_ms)
          break;
      }
    }
  } else {
    // Pure single frequency tone playback
    size_t total_samples = (size_t)((uint64_t)sample_rate * duration_ms / 1000);
    printf("test_sound: playing %d Hz tone for %d ms (total %zu samples)...\n",
           freq, duration_ms, total_samples);

    float phase = 0.0f;
    float phase_step = (float)(2.0 * M_PI * freq / sample_rate);

    // 10 ms linear attack and release envelope to suppress pop & clicks
    size_t fade_frames = (size_t)(sample_rate * 10 / 1000);
    if (fade_frames == 0)
      fade_frames = 1;
    if (fade_frames > total_samples / 2)
      fade_frames = total_samples / 2;

    while (frames_rendered < total_samples) {
      size_t chunk_frames = total_samples - frames_rendered;
      if (chunk_frames > buffer_frames)
        chunk_frames = buffer_frames;

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
        if (phase >= (float)(2.0 * M_PI))
          phase -= (float)(2.0 * M_PI);

        for (uint32_t ch = 0; ch < channels; ch++) {
          buffer[i * channels + ch] = val;
        }
      }

      gettimeofday(&tv_r1, NULL);
      int64_t r_us = (int64_t)(tv_r1.tv_sec - tv_r0.tv_sec) * 1000000LL + (tv_r1.tv_usec - tv_r0.tv_usec);
      total_render_us += r_us;
      if (r_us < min_render_us) min_render_us = r_us;
      if (r_us > max_render_us) max_render_us = r_us;

      size_t bytes_to_write = chunk_frames * channels * sizeof(int16_t);
      ssize_t written = write(fd, buffer, bytes_to_write);
      if (written < 0) {
        printf("test_sound: write error: %d (%s)\n", errno, strerror(errno));
        close(fd);
        return -1;
      }

      gettimeofday(&tv_w1, NULL);
      int64_t w_us = (int64_t)(tv_w1.tv_sec - tv_r1.tv_sec) * 1000000LL + (tv_w1.tv_usec - tv_r1.tv_usec);
      total_write_us += w_us;
      if (w_us < min_write_us) min_write_us = w_us;
      if (w_us > max_write_us) max_write_us = w_us;
      write_count++;

      frames_rendered += chunk_frames;

      int64_t now_us = (int64_t)tv_w1.tv_sec * 1000000LL + tv_w1.tv_usec;
      if (verbose && (now_us - last_checkpoint_us >= 100000 || frames_rendered >= total_samples)) {
        int64_t wall_ms = (now_us - t0_us) / 1000;
        int64_t audio_ms = (int64_t)frames_rendered * 1000 / sample_rate;
        int64_t lead_ms = audio_ms - wall_ms;
        printf("test_sound: [%4lld ms wall | %4lld ms audio] lead=%+3lld ms, render=%4lld us, write=%4lld us\n",
               wall_ms, audio_ms, lead_ms, r_us, w_us);
        last_checkpoint_us = now_us;
      }
    }
  }

  // Push 512 frames of clean silence to flush the DMA ring buffer cleanly into silence
  memset(buffer, 0, 512 * channels * sizeof(int16_t));
  write(fd, buffer, 512 * channels * sizeof(int16_t));

  gettimeofday(&tv_end, NULL);
  int64_t total_wall_us = (int64_t)(tv_end.tv_sec - tv_start.tv_sec) * 1000000LL + (tv_end.tv_usec - tv_start.tv_usec);

  eos_sound_stats_t stats = {0};
  int have_stats = (ioctl(fd, EOS_SOUND_IOCTL_GET_STATS, &stats) == 0);

  close(fd);
  printf("test_sound: playback complete.\n");

  int64_t audio_dur_us = (int64_t)frames_rendered * 1000000LL / sample_rate;
  int64_t final_lead_us = audio_dur_us - total_wall_us;
  double wall_ms = (double)total_wall_us / 1000.0;
  double render_ms = (double)total_render_us / 1000.0;
  double write_ms = (double)total_write_us / 1000.0;

  if (verbose) {
    printf("test_sound: ── Timing & Time Shift Analysis ───────────────────\n");
    printf("test_sound: target duration:     %d ms (%zu samples @ %lu Hz, %u ch)\n",
           duration_ms, frames_rendered, (unsigned long)sample_rate, (unsigned)channels);
    printf("test_sound: wall clock elapsed:  %.2f ms\n", wall_ms);
    printf("test_sound: synthesis (CPU):     %.2f ms (%.1f%% CPU, avg=%.1f us/chunk)\n",
           render_ms, (wall_ms > 0) ? (render_ms * 100.0 / wall_ms) : 0.0,
           write_count ? ((double)total_render_us / write_count) : 0.0);
    printf("test_sound: write blocking:      %.2f ms (%.1f%% blocked in DMA)\n",
           write_ms, (wall_ms > 0) ? (write_ms * 100.0 / wall_ms) : 0.0);
    printf("test_sound: write chunks:        %zu (min=%.2f ms, avg=%.2f ms, max=%.2f ms)\n",
           write_count, (double)min_write_us / 1000.0,
           write_count ? ((double)total_write_us / (write_count * 1000.0)) : 0.0,
           (double)max_write_us / 1000.0);
    printf("test_sound: pipeline time shift: %+.2f ms (buffered lead at exit)\n",
           (double)final_lead_us / 1000.0);
    if (have_stats) {
      printf("test_sound: driver metrics:      bclk=%lu Hz, dma=[%lux%lu], underruns=%lu, driver_lead=%+.2f ms\n",
             (unsigned long)stats.bclk_actual_hz, (unsigned long)stats.dma_desc_num,
             (unsigned long)stats.dma_frame_num, (unsigned long)stats.underruns,
             (double)stats.last_lead_us / 1000.0);
      printf("test_sound: driver latency:      lock_total=%.2f ms, shift_math=%.2f ms, i2s_wait=%.2f ms\n",
             (double)stats.total_lock_us / 1000.0,
             (double)stats.total_proc_us / 1000.0,
             (double)stats.total_i2s_wait_us / 1000.0);
    }
    printf("test_sound: ───────────────────────────────────────────────────\n");
  }

  return 0;
}

EOS_BIN_ATTR eos_bin_t test_sound = {
    EOS_BIN_INITIALIZER,
    .filename = "test_sound",
    .name = "Sound Test",
    .group = "Tests",
    .description = "Play a pure audio tone or Imperial March theme through /dev/sound0",
    .entry_point = test_sound_main};
