# Drivers

Drivers live as `<scope>/<name>/<name>.c`. Scope isn't just a folder — it's
an agreement: drivers in the same scope have to behave the same way from
the outside, so swapping one device for another doesn't change how the
device file is used. Two display drivers, whatever device they're actually
wrapping, expose the same kind of file — same write semantics, same ioctl
vocabulary, same general shape — so code talking to `/dev/display0`
doesn't care which one it's actually got.

Each scope directory has a `README.md` spelling out that agreement — what
a driver in it is expected to look like from the file interface side, and
where its device file ends up (e.g. `display` → `/dev/displayN`).

**Read the scope's README before adding a driver to it.** The point is
consistency across drivers in the same scope, not just code conventions.

New scope = write that agreement down first, then write the driver.

## Compile-time enable/disable

Every driver file wraps itself in an `#ifdef`:

```c
#ifdef DRIVER_STORAGE_SD_ENABLED
...
#endif
```

The flag name is always `DRIVER_<SCOPE>_<NAME>_ENABLED`, uppercased. This
is how a board/config decides which drivers actually end up in the
binary — disabled drivers cost nothing, not even dead code, since they're
never compiled.

## Registration

```c
EOS_DRIVER_REG(storage, sd, EOS_INIT_DRIVERS_PRIO);
```

This registers the driver into the system at startup, keyed by scope and
name. It's what makes device utilizing `driver_storage_sd_*` discoverable as `"/dev/storage0"`without anything else needing to know the driver exists at compile time — no central list to edit, no header to add an entry to. Add the driver file, guard it with its `#ifdef`, call `EOS_DRIVER_REG`, done.

The priority argument controls init ordering relative to other registered
drivers. Most drivers should use the default (`EOS_INIT_DRIVERS_PRIO`);
bump it only when ordering actually matters — e.g. a bus driver needs to
register and be ready before any device driver that's going to ride on it.