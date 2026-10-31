# Sound Driver Scope & Audio Architecture

Sound drivers integrate into the EOS Unified Device Model under the `sound` scope, registering device nodes (such as `/dev/sound0`) that accept digital PCM sample streams and hardware control requests.

---

## MAX98357A I2S Audio Driver (`sound/max98357`)

The MAX98357A driver interfaces with external I2S Class-D mono/stereo audio amplifiers and DACs using modern ESP-IDF `esp_driver_i2s` standard mode transmission with DMA-backed ring buffers.

### Device Node & Attachment
*   **Driver Scope:** `sound`
*   **Driver Name:** `max98357`
*   **Devfs Device Node:** `/dev/sound0` (indexed sequentially)
*   **Parent:** `root`
*   **Attachment Example:**
    ```sh
    devctl attach sound max98357 root --pin bclk=42,ws=1,dout=2 --cfg rate=22050,bits=16,channels=1,vol=100,mult=10,delay=90
    ```
    *(Note: Mono `channels=1` at `rate=22050` is the hardware-optimized default on Lilka. Stereo and other rates can be enabled by passing `--cfg channels=2` or `--cfg rate=44100` or via ioctl).*

### Lilka v2.3 Pinout Mapping
*   **BCLK (Bit Clock):** `GPIO 42` (also accepted as `sck`)
*   **WS / LRCK (Word Select / LR Clock):** `GPIO 1` (also accepted as `lrck`)
*   **DOUT (Data Out):** `GPIO 2` (also accepted as `data` or `din`)
*   **SD_MODE / Gain:** Hardwired on PCB (no MCU GPIO control required)

---

## Latency Tuning & Fast Bit-Shift Volume Scaling

The driver uses low-overhead bit-shift math and coarse power-of-2 DMA quad-buffering:

*   **Fast Power-of-2 Bit-Shift Volume Scaling:** Instead of expensive sample-by-sample 32-bit integer multiplication and division, volume and power attenuation are computed as single-cycle arithmetic bit-shifts (`>> shift` in logarithmic -6 dB steps).
*   **Quad-Buffered DMA Layout:** The driver employs a fixed 4-descriptor architecture (`dma_desc_num = 4`), scaling descriptor frame lengths in clean multiples of 64 frames (default 512 frames at 22,050 Hz, providing ~92.8 ms of smooth quad-buffered audio at 43 Hz interrupt rate with only 4 KB RAM).
*   **Reduced Context Switching:** Large 512-sample (1024-byte) processing chunks eliminate 75% of `i2s_channel_write()` calls and FreeRTOS semaphore yields.
*   **Pop & Click Suppression Architecture:**
    *   **DMA Auto-Clear (`chan_cfg.auto_clear = true`):** The driver configures ESP-IDF to automatically zero completed DMA buffers, preventing old audio data from repeating in a loop upon stream completion or temporary underrun.
    *   **Attack & Release Enveloping:** Digital synthesis in user utilities (`test_sound`) applies a 10 ms linear attack and release envelope to prevent abrupt step-function voltage discontinuities ($\Delta V$) at waveform start and finish.
    *   **Buffer-Aligned Writes:** User-space chunk transfers are aligned to DMA descriptor frame boundaries (512 frames / 1024 bytes), preventing ESP-IDF's internal buffer discarding.
    *   **Trailing Buffer Flushing:** Writers feed a trailing zero-buffer block to cleanly flush the remaining pipeline into true silence before closing device handles.

---

## Why 22,050 Hz is Default & Problems with 44,100 Hz on Handhelds

While the driver supports 44,100 Hz (and 48,000 Hz), **22,050 Hz is the standard default for EOS on Lilka v2.3 and ESP32-S3 handheld devices**. Comprehensive empirical profiling identified the following critical bottlenecks with 44,100 Hz:

### 1. Internal DMA SRAM Consumption
* **At 22,050 Hz:** A ~92.8 ms glitch-proof buffer requires only **4 KB of internal DMA RAM** (`4 descriptors x 512 frames x 2 bytes`).
* **At 44,100 Hz:** The same 92.8 ms buffer requires **8 KB of internal DMA RAM** (`4 descriptors x 1024 frames x 2 bytes`).
* On memory-constrained microcontrollers where internal SRAM is shared with WiFi buffers, Bluetooth, and display framebuffers, saving 4 KB of contiguous internal DMA memory is significant.

### 2. Audio Synthesis & Driver CPU Overhead
* **Sample Rate Scaling:** 44,100 Hz requires generating and processing **twice as many audio samples per second** (44,100 vs 22,050).
* **Sine Synthesis Benchmark:** Generating a 1-second sine wave on the ESP32-S3 requires **48.8 ms** of CPU time at 44.1 kHz, compared to **24.4 ms** at 22.05 kHz (a 50% reduction in CPU cycles).
* **Driver Math & Volume Scaling:** Logarithmic volume shifting and byte alignment checks take 2.30 ms at 44.1 kHz vs 1.10 ms at 22.05 kHz.

### 3. The ESP-IDF 2-Descriptor Buffer Trap
When trying to reduce RAM usage at 44,100 Hz by reducing descriptor count instead of sample rate (e.g. `2x2048` to achieve 8 KB RAM):
* ESP-IDF's I2S driver (`i2s_common.c` line 1361) enforces the queue condition:
  `if (handle->dma.rw_pos == handle->dma.buf_size || handle->dma.curr_ptr == NULL || uxQueueSpacesAvailable(handle->msg_queue) <= 1)`
* With `dma_desc_num = 2`, the queue capacity is 2. While the DMA is executing, `uxQueueSpacesAvailable` is almost always $\le 1$.
* Consequently, every non-aligned user write blocks until an entire descriptor drains. In testing, playing a 1-second audio tone with a `2x2048` layout took **7.96 seconds** (audio stretched by ~800% with 169 underruns!).
* Therefore, the DMA pipeline **strictly requires $\ge$ 4 descriptors** for smooth asynchronous chunk writes, making sample-rate reduction the correct architectural solution to reduce RAM footprint.

### 4. Hardware GDMA 12-Bit Buffer Limit
* The ESP32-S3 GDMA hardware descriptor length field is 12 bits wide, limiting a single descriptor buffer to **4,092 bytes**.
* A buffer of 2,048 stereo or 16-bit frames (4,096 bytes) exceeds this hardware ceiling, forcing ESP-IDF to clamp to 2,046 frames.
* At 22,050 Hz, 512 frames equal 1,024 bytes per descriptor, well clear of any hardware truncation boundaries.

### 5. Acoustic Matching with Miniature Handheld Speakers
* Lilka v2.3 features a miniature onboard speaker whose acoustic frequency response rolls off steeply above **8–10 kHz**.
* The Nyquist limit of 22,050 Hz is **11,025 Hz**, which perfectly covers the physical acoustic reproduction capability of the onboard speaker.
* Running at 44,100 Hz reproduces frequencies up to 22.05 kHz that the speaker physically cannot emit, wasting battery power, CPU cycles, and DMA memory on ultrasound.

---

## Audio Streaming & Power Multiplier

Because the MAX98357A delivers up to 3.2W of output power, the driver incorporates a hardware power multiplier (`mult`) on top of the user volume to protect hearing and prevent dangerously loud audio in handheld/earplugs use:

*   **Earplugs Mode (Default):** Caps total amplitude to **10%** of hardware maximum (`mult=10`).
*   **Configurable Multiplier:** Can be tuned from 1% to 100% via `--cfg mult=<percent>` or changed dynamically at runtime via `EOS_SOUND_IOCTL_SET_MULT`.
*   **Full 3W Speaker Mode:** Pass `--cfg mult=100` or `--cfg earplugs=0` during attachment to uncap full 3W output.
*   **Digital Scaling Math:**
    $$\text{effective\_gain} = \frac{\text{volume} \times \text{mult} \times 1024}{10000}$$
    Scaled dynamically using fast fixed-point integer arithmetic before DMA streaming.
*   **Concurrency:** FreeRTOS mutex synchronization guarantees thread-safe multi-process audio streaming.

---

## Control Operations (`ioctl`)

The sound subsystem supports standard `ioctl` calls defined in `ecore/ioctl.h`:

| IOCTL Command | Argument Type | Description |
| :--- | :--- | :--- |
| `EOS_SOUND_IOCTL_GET_CONFIG` | `eos_sound_config_t *` | Retrieves sample rate, bit depth, channel count, volume, and power multiplier |
| `EOS_SOUND_IOCTL_SET_CONFIG` | `const eos_sound_config_t *` | Atomically applies all audio format and multiplier parameters |
| `EOS_SOUND_IOCTL_SET_SAMPLE_RATE` | `uint32_t` | Reconfigures clock sample rate (e.g. 8000–48000 Hz) |
| `EOS_SOUND_IOCTL_GET_SAMPLE_RATE` | `uint32_t *` | Queries current clock sample rate in Hz |
| `EOS_SOUND_IOCTL_SET_BITS_PER_SAMPLE` | `uint32_t` | Reconfigures slot bit depth (16, 24, 32-bit) |
| `EOS_SOUND_IOCTL_GET_BITS_PER_SAMPLE` | `uint32_t *` | Queries current slot bit depth |
| `EOS_SOUND_IOCTL_SET_CHANNELS` | `uint32_t` | Sets mono (1) or stereo (2) channel format |
| `EOS_SOUND_IOCTL_GET_CHANNELS` | `uint32_t *` | Queries current channel count |
| `EOS_SOUND_IOCTL_SET_VOLUME` | `uint32_t` | Sets software user volume (0 to 100) |
| `EOS_SOUND_IOCTL_GET_VOLUME` | `uint32_t *` | Queries current user volume level |
| `EOS_SOUND_IOCTL_SET_MULT` | `uint32_t` | Sets power ceiling multiplier (1 to 100, default 10 for earplugs mode) |
| `EOS_SOUND_IOCTL_GET_MULT` | `uint32_t *` | Queries current power multiplier percentage |
| `EOS_SOUND_IOCTL_START` | `none` | Enables I2S TX channel output |
| `EOS_SOUND_IOCTL_STOP` | `none` | Disables I2S TX channel output |
| `EOS_SOUND_IOCTL_GET_DELAY` | `uint32_t *` | Queries current audio buffer delay in milliseconds |
| `EOS_SOUND_IOCTL_SET_DELAY` | `uint32_t` | Reconfigures audio delay in ms and recomputes DMA buffers |
| `EOS_SOUND_IOCTL_GET_STATS` | `eos_sound_stats_t *` | Queries driver runtime metrics, timing stats, BCLK frequency, and time shifts |

---

## Testing Utilities & Melodic Synth
 
The `test_sound` binary in user space (`ebin`) provides waveform synthesis, time-shift measurement, and melodic playback:
```sh
test_sound [freq_hz] [duration_ms] [volume] [multiplier] [flags] [device_path]
test_sound march [duration_ms] [volume] [flags]

# Examples:
# 1. Play a 440 Hz tone for 500 ms at 30% volume
test_sound 440 500 30

# 2. Use -t / --time flag to set playback duration explicitly (e.g. 2.5 seconds)
test_sound 440 -t 2500 30

# 3. Play Star Wars Imperial March theme for 3.2 seconds at 30% volume
test_sound march -t 3200 30

# 4. Display detailed per-chunk time shift logs during playback
test_sound 440 1000 30 --verbose
```

---
[Back to EOS Project Index](../Readme.md) | [See Storage Drivers](./storage-driver-scope.md)
