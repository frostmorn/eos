# Bus scope

A bus is any device whose driver has `scope == "bus"` — this is a
topological role, not a claim about physical hardware. A composite USB
device, for instance, is one physical device but logically a bus: it's
the origin point for its own interfaces, so it registers under `bus`
scope like SPI or GPIO would. `root` is the special case with no
hardware behind it at all — just the tree's origin point.

Only bus-scoped devices can have children. `eos_dev_attach` checks
`parent->driver->is_bus` before doing anything else and fails with
`EOS_NOT_A_BUS_ERROR` if the target isn't one.

## Attach requires permission

Before linking a child into the tree, EOS calls the parent's `ioctl`
with `EOS_BUS_IOCTL_KID_ATTACH` (see `bus.h`). The bus can decline —
e.g. GPIO checking whether a pin is already claimed — by returning
anything other than `EOS_NO_ERROR`; the attach aborts and the child
never enters the tree. A bus with nothing to check (root) must still
explicitly return `EOS_NO_ERROR`, not silently ignore the command.

On success, the bus may assign the child an index via the ioctl's
out-param — this becomes `child->index`, stable for the child's
lifetime. Buses that don't care about indexing return `0`.

`EOS_BUS_IOCTL_KID_DETACH` is notify-only — a bus can veto an attach
but not a detach.

## Locking

Buses where children share one physical channel and can't be talked to
concurrently (SPI is the canonical case) must implement
`EOS_BUS_IOCTL_LOCK`/`UNLOCK`. Drivers for devices on such a bus must
lock before `read`/`write` and unlock after. Buses without this
requirement (GPIO, root) still implement both as no-ops returning
`EOS_NO_ERROR`, so device drivers can call lock/unlock unconditionally
without needing to know which kind of bus they're on.

## Reserved ioctl range

Commands below `EOS_BUS_IOCTL_USER_BASE` (see `bus.h`) are
framework-reserved and apply to every bus. Bus-specific commands (SPI
mode, GPIO direction, ...) must start at `EOS_BUS_IOCTL_USER_BASE`.

## Adding a bus driver

Standard process from `drivers/Readme.md`: guard with
`EOS_DRIVER_BUS_<NAME>_ENABLED`, register with `EOS_DRIVER_REG(bus, <name>,
priority)`. Buses other devices attach to at boot should register
early — see `initprio.h`.