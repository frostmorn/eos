#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "ecore/bin.h"
#include "ecore/ioctl.h"

int test_sound_main(int argc, char **argv) {
  int freq = 440;
  int duration_ms = 1000;
  int volume = -1;
  int mult = -1;
  const char *path = "/dev/sound0";

  if (argc > 1) {
    if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
      printf("Usage: test_sound [freq_hz] [duration_ms] [volume 0-100] [mult 0-100] [path]\n");
      return 0;
    }
    freq = atoi(argv[1]);
    if (freq <= 0)
      freq = 440;
  }
  if (argc > 2) {
    duration_ms = atoi(argv[2]);
    if (duration_ms <= 0)
      duration_ms = 1000;
  }
  if (argc > 3) {
    volume = atoi(argv[3]);
    if (volume < 0)
      volume = 0;
    if (volume > 100)
      volume = 100;
  }
  if (argc > 4) {
    // If 4th arg looks like a device path, use it as path; otherwise as mult
    if (argv[4][0] == '/') {
      path = argv[4];
    } else {
      mult = atoi(argv[4]);
      if (mult < 0)
        mult = 0;
      if (mult > 100)
        mult = 100;
    }
  }
  if (argc > 5) {
    path = argv[5];
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
    printf("test_sound: current config: rate=%lu Hz, bits=%u, channels=%u, vol=%u, mult=%u%%\n",
           (unsigned long)cfg.sample_rate, cfg.bits_per_sample, cfg.channels,
           cfg.volume, cfg.mult);
  } else {
    cfg.sample_rate = 44100;
    cfg.channels = 1;
    cfg.mult = 10;
  }

  if (mult >= 0) {
    printf("test_sound: setting power multiplier to %d%%...\n", mult);
    ioctl(fd, EOS_SOUND_IOCTL_SET_MULT, (uint32_t)mult);
  }

  if (volume >= 0) {
    printf("test_sound: setting volume to %d...\n", volume);
    ioctl(fd, EOS_SOUND_IOCTL_SET_VOLUME, (uint32_t)volume);
  }

  uint32_t sample_rate = cfg.sample_rate ? cfg.sample_rate : 44100;
  uint32_t channels = cfg.channels ? cfg.channels : 2;
  size_t total_samples = (size_t)((uint64_t)sample_rate * duration_ms / 1000);

  printf("test_sound: playing %d Hz tone for %d ms (total %zu samples)...\n",
         freq, duration_ms, total_samples);

  int16_t buffer[256];
  size_t buffer_frames = sizeof(buffer) / (sizeof(int16_t) * channels);
  size_t frames_rendered = 0;
  float phase = 0.0f;
  float phase_step = (float)(2.0 * M_PI * freq / sample_rate);

  while (frames_rendered < total_samples) {
    size_t chunk_frames = total_samples - frames_rendered;
    if (chunk_frames > buffer_frames)
      chunk_frames = buffer_frames;

    for (size_t i = 0; i < chunk_frames; i++) {
      int16_t val = (int16_t)(sinf(phase) * 16000.0f);
      phase += phase_step;
      if (phase >= (float)(2.0 * M_PI))
        phase -= (float)(2.0 * M_PI);

      for (uint32_t ch = 0; ch < channels; ch++) {
        buffer[i * channels + ch] = val;
      }
    }

    size_t bytes_to_write = chunk_frames * channels * sizeof(int16_t);
    ssize_t written = write(fd, buffer, bytes_to_write);
    if (written < 0) {
      printf("test_sound: write error: %d (%s)\n", errno, strerror(errno));
      close(fd);
      return -1;
    }

    frames_rendered += chunk_frames;
  }

  close(fd);
  printf("test_sound: playback complete.\n");
  return 0;
}

EOS_BIN_ATTR eos_bin_t test_sound = {
    EOS_BIN_INITIALIZER,
    .filename = "test_sound",
    .name = "Sound Test",
    .group = "Tests",
    .description = "Play a pure audio tone through /dev/sound0",
    .entry_point = test_sound_main};
