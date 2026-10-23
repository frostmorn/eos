# Sound Driver Scope & Audio Architecture

Sound drivers (such as the MAX98357 audio codec interfacing via I2S, I2C, or SPI) integrate seamlessly into the EOS Unified Device Model by registering as peripherals attached to a parent bus node.

---

## Implementation Details

*   **Bus Attachment:** Audio peripherals attach to bus nodes (such as I2S or I2C capability buses) during device tree initialization.
*   **PCM Data Streams:** Drivers implement standard `read` and `write` operations to stream digital audio (PCM samples) directly to hardware buffers or DAC/codec peripherals.
*   **Device Control (`ioctl`):** Audio settings such as sample rate, bit depth, volume, and gain are configured dynamically via standard `ioctl` commands.

---
[Back to EOS Project Index](../Readme.md) | [See Storage Drivers](./storage-driver-scope.md)
