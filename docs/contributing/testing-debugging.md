---
title: Testing & Debugging
parent: Contributing
nav_order: 4
---

# Testing and Debugging

CrossInk runs on real hardware, so debugging usually combines local build checks, simulator checks, and on-device logs.

## Local checks

Make sure `clang-format` 21+ is installed and available in `PATH` before running the formatting step.
If needed, see [Getting Started](./getting-started.md).

```sh
./bin/clang-format-fix
pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high
pio run -e simulator
pio run -e default
```

`pio run` without `-e` builds the release matrix from `platformio.ini` (`teensy`, `tiny`, and `xlarge`). Use it before opening broad firmware PRs, but prefer explicit environments while iterating.

## Simulator smoke test

The simulator can run a headless smoke flow that exercises Home, Flashcards,
File Browser, Recents, Settings, Reader Options, Reader Menu, and Sleep without
flashing a device.

```sh
pio run -e simulator
SIM_SD=$(mktemp -d /tmp/crossink-smoke-sd.XXXXXX)
CROSSINK_SIMULATOR_SMOKE_TEST=1 \
  CROSSINK_SIMULATOR_SMOKE_PAGE_TURNS=0 \
  CROSSPOINT_SIM_SD="$SIM_SD" \
  .pio/build/simulator/program
```

The smoke test creates a temporary flashcard deck in the simulated SD card and
checks the basic deck-list, flip, rating, and return-to-list flow.

## Firmware build artifacts

Build a hardware firmware image without flashing:

```sh
pio run -e tiny
```

The copied artifact is written to:

```text
.pio/build/tiny/firmware-tiny.bin
```

This command only builds the binary. It does not upload firmware unless
`--target upload` is added.

## Flash and monitor

Flash firmware:

```sh
pio run -e default --target upload
```

Open serial monitor:

```sh
pio device monitor
```

Optional enhanced monitor:

```sh
python3 -m pip install pyserial colorama matplotlib
python3 scripts/debugging_monitor.py
```

## Useful bug report contents

- Firmware version and build environment
- Exact steps to reproduce
- Expected vs actual behavior
- Serial logs from boot through failure
- Whether issue reproduces after clearing the affected book cache or using **Clear Reading Cache**

## Common troubleshooting references

- [Common Issues](../troubleshooting.md)
