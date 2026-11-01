# Sound Driver Contracts & Audio Architecture

Audio drivers in EOS reside under `scope == "sound"`, registering device nodes (such as `/dev/sound0`) that stream digital PCM sample blocks to audio amplifiers and DACs.

---

## 1. Supported Audio Driver (`main/edriver/sound/`)

* **Driver Name:** `max98357`
* **Device Node:** `/dev/sound0`
* **Hardware Interface:** Modern ESP-IDF standard mode I2S transmission (`esp_driver_i2s`) with DMA quad-buffering.
* **Target Hardware:** MAX98357A Class-D mono audio amplifier on Lilka v2.3 (and compatible I2S DACs).

---

## 2. VFS Streaming Contract

* **`open("/dev/sound0", O_WRONLY)`**: Acquires audio driver mutex lock and activates I2S TX channel.
* **`write(fd, pcm_buffer, bytes)`**: Writes raw signed 16-bit PCM samples into the driver.
  * Writes are processed in 512-sample (1024-byte) aligned chunks.
  * Fast bit-shift scaling scales volume and hardware multiplier before DMA submission.
  * Automatically blocks in `i2s_channel_write()` until DMA descriptors become available.
* **`close(fd)`**: Flushes trailing DMA silence and de-activates transmission.

---

## 3. Audio IOCTL Interface

All sound subsystem IOCTLs reside in range `400–499`:

| IOCTL Command | Numeric Code | Argument Type | Description |
| :--- | :---: | :--- | :--- |
| `EOS_SOUND_IOCTL_GET_CONFIG` | `400` | `eos_sound_config_t *out_cfg` | Retrieves current sample rate, bit depth, channels, volume, and multiplier. |
| `EOS_SOUND_IOCTL_SET_CONFIG` | `401` | `const eos_sound_config_t *cfg` | Atomically applies full audio format and multiplier configuration. |
| `EOS_SOUND_IOCTL_SET_SAMPLE_RATE` | `402` | `uint32_t rate_hz` | Reconfigures clock rate (8,000 Hz to 48,000 Hz; default 22,050 Hz). |
| `EOS_SOUND_IOCTL_GET_SAMPLE_RATE` | `403` | `uint32_t *out_rate_hz` | Queries current clock rate in Hz. |
| `EOS_SOUND_IOCTL_SET_BITS_PER_SAMPLE` | `404` | `uint32_t bits` | Configures sample bit depth (16, 24, 32-bit; default 16). |
| `EOS_SOUND_IOCTL_GET_BITS_PER_SAMPLE` | `405` | `uint32_t *out_bits` | Queries current bit depth. |
| `EOS_SOUND_IOCTL_SET_CHANNELS` | `406` | `uint32_t channels` | Configures mono (`1`) or stereo (`2`) mode. |
| `EOS_SOUND_IOCTL_GET_CHANNELS` | `407` | `uint32_t *out_channels` | Queries current channel count. |
| `EOS_SOUND_IOCTL_SET_VOLUME` | `408` | `uint32_t volume` | Configures user volume percentage (`0` to `100`). |
| `EOS_SOUND_IOCTL_GET_VOLUME` | `409` | `uint32_t *out_volume` | Queries user volume percentage. |
| `EOS_SOUND_IOCTL_SET_MULT` | `410` | `uint32_t mult` | Sets hardware power multiplier (`1` to `100`; default `10` for earplugs protection). |
| `EOS_SOUND_IOCTL_GET_MULT` | `411` | `uint32_t *out_mult` | Queries active power multiplier percentage. |
| `EOS_SOUND_IOCTL_START` | `412` | `none` | Enables I2S TX channel clocking. |
| `EOS_SOUND_IOCTL_STOP` | `413` | `none` | Disables I2S TX channel clocking. |
| `EOS_SOUND_IOCTL_GET_DELAY` | `414` | `uint32_t *out_delay_ms` | Queries target DMA buffer delay in milliseconds. |
| `EOS_SOUND_IOCTL_SET_DELAY` | `415` | `uint32_t delay_ms` | Reconfigures buffer delay and reallocates DMA descriptors. |
| `EOS_SOUND_IOCTL_GET_STATS` | `416` | `eos_sound_stats_t *out_stats` | Queries driver runtime metrics, timing stats, BCLK frequency, and lead/lag. |

### Configuration Structures (`ecore/ioctl.h`)
```c
typedef struct {
  uint32_t sample_rate;    // Clock sample rate (default 22050)
  uint8_t bits_per_sample; // Bit depth (default 16)
  uint8_t channels;        // 1 (mono) or 2 (stereo)
  uint8_t volume;          // User volume 0 - 100
  uint8_t mult;            // Power multiplier % (default 10)
  uint16_t delay_ms;       // Target buffer delay in ms (default 90)
} eos_sound_config_t;
```

---

## 4. Latency Architecture & Acoustic Matching

### 1. Fast Bit-Shift Volume Scaling
Instead of slow sample-by-sample 32-bit multiplication and division, attenuation is calculated via single-cycle arithmetic bit-shifts (`>> shift` in logarithmic -6 dB steps).

### 2. Quad-Buffered DMA Pipeline (`dma_desc_num = 4`)
The driver maintains a 4-descriptor architecture. A minimum of 4 descriptors is mathematically required in ESP-IDF to prevent queue exhaustion deadlocks where writes block on draining descriptors.

### 3. Pop & Click Suppression
* **Auto-Clear (`chan_cfg.auto_clear = true`):** Completed DMA buffers are automatically zeroed by hardware, preventing old waveform looping upon stream completion.
* **Linear Attack/Release Enveloping:** Waveform generators (`test_sound`) apply 10 ms linear ramp-up and ramp-down curves, preventing step-function voltage discontinuities ($\Delta V$).

### 4. Why 22,050 Hz is Default
* **DMA RAM Savings:** At 22,050 Hz, a 92.8 ms glitch-proof buffer consumes only **4 KB internal DMA SRAM** (`4 descriptors x 512 frames x 2 bytes`), compared to 8 KB at 44,100 Hz.
* **CPU Reduction:** Synthesis and volume mathematics consume 50% fewer CPU cycles.
* **Acoustic Rolloff:** Miniature handheld speakers roll off steeply above 8–10 kHz. The Nyquist ceiling of 22,050 Hz is 11,025 Hz, perfectly matching speaker acoustics while eliminating wasted battery and ultrasound energy.

---

## 5. Declarative Attachment Syntax

```sh
# Lilka v2.3: MAX98357A on GPIO 42 (bclk), GPIO 1 (ws), GPIO 2 (dout)
devctl attach sound max98357 root \
  --pin bclk=42,ws=1,dout=2 \
  --cfg rate=22050,bits=16,channels=1,vol=100,mult=10,delay=90
```
