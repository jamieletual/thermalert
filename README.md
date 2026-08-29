# Thermalert

Thermalert is a lightweight temperature monitor for Ubuntu 24.04. It
runs as a small background desktop application, reads hardware temperatures
through `libsensors`, and exposes its status through an Ayatana AppIndicator in
the GNOME top bar.

The application is developed and tested on Ubuntu 24.04 with GNOME. The
development packages required to build it are listed below.

## User experience

Thermalert remains unobtrusive during normal operation. The top bar displays the
primary sensor temperature beside a symbolic thermometer that communicates the
current state:

- **Normal:** neutral symbolic icon
- **Warning:** amber symbolic icon
- **Critical:** red symbolic icon

Opening the indicator menu shows the overall status, primary temperature,
available individual sensor readings, **Temperature Monitor…**,
**Preferences…**, **About Thermalert**, and **Quit**. The temperature monitor
can graph multiple sensors over a five-minute window and reports current,
minimum, and maximum values from its bounded in-memory history. Exact icon
rendering, placement, and menu styling depend on the installed GNOME Shell
theme and AppIndicator extension.

The application uses three temperature states, plus an Unknown operational
state, and evaluates the selected primary temperature sensor:

| State | Default transition |
| --- | ---: |
| Normal | Below 80 °C |
| Warning | At or above 80 °C |
| Critical | At or above 100 °C |

A default hysteresis of 5 °C prevents rapid state changes. For example, after
entering Warning at 80 °C, Thermalert returns to Normal only after the relevant
temperature falls below 75 °C. A notification is generated only when the state
crosses into Critical, not on every polling cycle while it remains critical.

By default, Thermalert selects a primary sensor automatically. It prefers Intel
CPU package readings, then AMD `Tctl` or `Tdie`, firmware-provided `CPU`
readings, and individual CPU cores. If none are available, it uses the hottest
valid temperature reading. A specific discovered sensor can be selected in
Preferences. If that sensor temporarily disappears, Thermalert preserves the
choice, falls back to automatic selection, and restores it when it returns. If
no temperature reading is available, the indicator uses the gray **Unknown**
state.

## Preferences

The preferences window exposes:

- Primary temperature sensor (automatic or a specific discovered sensor)
- Start automatically when the user logs in
- Warning temperature
- Critical temperature
- Hysteresis
- Polling interval (5 seconds by default)

Values are validated, including the requirement that the critical temperature
be higher than the warning temperature. Sensor and threshold preferences are
stored with GSettings and applied without restarting the application. The
autostart option directly manages the user's XDG autostart entry.

## Platform and dependencies

The supported target is Ubuntu 24.04 with GNOME. Thermalert is written in C and
built with GCC and Make using:

- GTK 3
- `libsensors` / lm-sensors
- Ayatana AppIndicator
- libnotify
- GLib and GSettings

Install the build dependencies and the `sensors` diagnostic utility with:

```sh
sudo apt update
sudo apt install build-essential pkg-config libsensors-dev lm-sensors \
  libgtk-3-dev libayatana-appindicator3-dev libnotify-dev libglib2.0-dev
```

## Configuration, build, and use

The source tree includes a lightweight POSIX shell `configure` script. It
checks for a usable C compiler, Make, pkg-config, required headers, and
linkable libraries before generating `Makefile` from `Makefile.in`. In
particular, dependencies without reliable pkg-config metadata are verified
with a small compile-and-link test rather than assumed to exist.

Build and run the non-GUI checks with:

```sh
./configure
make
make check
```

`configure` accepts `--prefix=PATH` and respects conventional build
environment variables such as `CC`, `CPPFLAGS`, `CFLAGS`, `LDFLAGS`, and
`PKG_CONFIG`. A failed check names the missing component and, where
practical, the corresponding Ubuntu development package.

Run from the source tree with:

```sh
make run
```

This compiles the local GSettings schema and sets the required schema and icon
paths without requiring a system-wide installation.

Install under `/usr/local` (the default prefix) with:

```sh
sudo make install
```

Use `./configure --prefix=/another/path` to choose another prefix. `DESTDIR` is
supported for packaging and staged installations. Remove installed files with
`sudo make uninstall`, and remove local build products with `make clean`.

## Debian package

Build the Ubuntu 24.04 package with:

```sh
dpkg-buildpackage --build=binary --no-sign
```

The Debian build additionally requires `debhelper` and `dpkg-dev`; `lintian`
can be used to inspect the resulting package.

The build runs the test suite and writes the architecture-specific package to
the parent directory. On an amd64 system, install or replace the same package
version with:

```sh
sudo apt install --reinstall ../thermalert_0.1.0-1_amd64.deb
```

The package installs the executable under `/usr/bin`, desktop and icon assets,
the GSettings schema, documentation, and an inactive autostart template. It
does not enable autostart during installation.

## Releases

`VERSION` is the authoritative upstream application version. Before a release,
the corresponding version in `debian/changelog`, the manual page, and package
examples in this README must agree with it. Check a proposed release tag with:

```sh
./scripts/check-release-version v0.1.0
```

Releases use signed annotated tags named `vMAJOR.MINOR.PATCH`. Pushing a matching
tag runs the complete build, test, metadata-validation, Debian-package, and
Lintian pipeline. If every step succeeds, GitHub Actions creates a draft GitHub
Release with generated notes, package and debug-symbol files, build metadata,
and SHA-256 checksums. Inspect the draft and its assets before publishing it.

## Autostart

Autostart can be enabled or disabled with the checkbox in Preferences.
Thermalert manages the user's XDG autostart entry at
`~/.config/autostart/com.thermalert.Thermalert-autostart.desktop`. Installing
the application does not enable autostart without the user's explicit choice.

## Architecture

The implementation uses the GLib main loop with one replaceable sensor-polling
timer and a short-lived startup label synchronization source. No worker thread
is used because reading local sensors is inexpensive. Responsibilities are
separated without introducing a large framework:

```text
src/
  main.c          application startup, shutdown, and coordination
  sensors.c       libsensors initialization and temperature enumeration
  history.c       bounded per-sensor temperature history
  thermal.c       state transitions, thresholds, and hysteresis
  indicator.c     AppIndicator menu and notification presentation
  monitor.c       real-time multi-sensor graph and statistics
  preferences.c   GTK preferences, validation, and autostart control
include/
  history.h
  sensors.h
  thermal.h
  indicator.h
  monitor.h
  preferences.h
data/
  com.thermalert.Thermalert.gschema.xml
  com.thermalert.Thermalert.desktop
  com.thermalert.Thermalert-autostart.desktop
  icons/             indicator icons for all four display states
tests/
  test-thermal.c     threshold and hysteresis tests
  test-sensors.c     enumeration and cross-platform selection tests
  test-history.c     bounded history and missing-sample tests
debian/              Debian source-package metadata
configure
Makefile.in
Makefile             generated by configure; not maintained by hand
LICENSE
README.md
```

Sensor acquisition retains chip/device identity, feature labels, and
current readings without assuming particular CPU, GPU, or hwmon names. Failed
readings and sensors that appear or disappear are tolerated. Notification
dispatch consumes state transitions, leaving room for future cooldown or
escalation policies.

## Inspecting and troubleshooting sensors

To see readings currently exposed by lm-sensors, run:

```sh
sensors
```

If no useful hardware sensors appear, `sensors-detect` can probe for additional
kernel modules:

```sh
sudo sensors-detect
```

Review its recommendations before loading modules or changing system
configuration. Some hardware, virtual machines, containers, proprietary GPU
drivers, and systems without supported kernel drivers may expose few or no
temperature readings. Thermalert uses the same underlying sensor interfaces
as the `sensors` command, but accesses them directly through `libsensors`.

## License

Copyright (C) 2026 Jamie Le Tual.

Thermalert is free software licensed under the GNU General Public License,
version 3. See [LICENSE](LICENSE) for the complete license terms.

## Future possibilities

The first version does not include per-sensor thresholds, persistent logging,
remote alerts, notification escalation, sustained temperature detection, or
launching Psensor. The internal boundaries above are intended to allow those
features later without complicating the initial utility.
