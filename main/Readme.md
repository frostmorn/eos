# EOS Device Model

Everything in EOS that isn't core kernel logic is a device: sensors, buses,
displays, storage, filesystems. Devices are chaotic by nature — different
shapes, different capabilities — and EOS's job is to give that chaos one
consistent shape from the outside.

Every device is two things at once:

1. **A node in the EOS device tree** (`eos_dev_t`) — attached to a parent,
   optionally parent to children of its own. This is how EOS tracks what
   exists, how it's wired together, and what depends on what for init/
   shutdown ordering.
2. **A file**, exposed for basic I/O (`read`/`write`). This keeps the common
   case — "read some bytes from this thing" — simple and uniform across
   wildly different hardware.

The file interface is deliberately minimal and won't fit every device
perfectly. Anything that doesn't fit `read`/`write` goes through `ioctl` —
a device-specific escape hatch for configuration and operations the basic
I/O model can't express.

Every device is backed by a **driver** (`eos_driver_t`) implementing
`init`, `read`, `write`, `ioctl`, and `shutdown`. Drivers live under
`drivers/<scope>/<name>/<name>.c` and self-register at startup via
`EOS_DRIVER_REG(scope, name, priority)` — see `drivers/Readme.md` for the
registration contract, and the relevant `drivers/<scope>/Readme.md` for
what a driver in that scope is expected to guarantee.