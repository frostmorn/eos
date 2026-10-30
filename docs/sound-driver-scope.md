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
    devctl attach sound max98357 root --pin bclk=42,ws=1,dout=2 --cfg rate=44100,bits=16,channels=1,vol=100,mult=10
    ```
    *(Note: Mono `channels=1` is the hardware-optimized default on Lilka. Stereo can be enabled by passing `channels=2` or via ioctl).*

### Lilka v2.3 Pinout Mapping
*   **BCLK (Bit Clock):** `GPIO 42` (also accepted as `sck`)
*   **WS / LRCK (Word Select / LR Clock):** `GPIO 1` (also accepted as `lrck`)
*   **DOUT (Data Out):** `GPIO 2` (also accepted as `data` or `din`)
*   **SD_MODE / Gain:** Hardwired on PCB (no MCU GPIO control required)

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

---

## Testing Utilities

The `test_sound` binary in user space (`ebin`) provides waveform synthesis for verification:
```sh
test_sound [freq_hz] [duration_ms] [volume] [multiplier] [device_path]
# Example: Play a 440 Hz tone for 500 ms at 100% volume with default 10% earplugs multiplier
test_sound 440 500 100

# Example: Test custom 25% multiplier
test_sound 440 500 100 25
```

---
[Back to EOS Project Index](../Readme.md) | [See Storage Drivers](./storage-driver-scope.md)
