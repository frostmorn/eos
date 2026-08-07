# Sound Driver Scope

Sound drivers (e.g., MAX98357 codec via I2C/SPI) integrate with the EOS device model by acting as peripherals attached to a bus node.

## Implementation Details

*   **Attachment:** Sound devices are attached to an audio or I2C/SPI bus node.
*   **Data Stream:** Drivers implement `read` and `write` to stream PCM data to the hardware.
*   **Control:** Use `ioctl` for configuration (e.g., volume, sample rate, gain).

---
[Back to EOS Project Index](../Readme.md) | [See Storage Drivers](./storage-driver-scope.md)
