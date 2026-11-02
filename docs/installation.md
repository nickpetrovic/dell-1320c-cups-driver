# Installation

Install the package for your system, or build the driver from source. Then create a print queue with the `dell1320c` command, as described in [Printer setup](printer-setup.md).

## Packages

| System | Package |
|---|---|
| macOS 13.3 or newer | `Dell1320c-0.1.0.pkg`, for Apple silicon and Intel Macs |
| Debian 12 or newer, Ubuntu 22.04 or newer | `dell1320c_0.1.0-1_amd64.deb` or `dell1320c_0.1.0-1_arm64.deb` |
| Fedora, RHEL 9.7 or newer, and their rebuilds at the same release | `dell1320c-0.1.0-1.x86_64.rpm` or `dell1320c-0.1.0-1.aarch64.rpm` |
| Other Linux with glibc 2.35 or newer | `dell1320c-0.1.0-linux-x86_64.tar.gz` or `dell1320c-0.1.0-linux-aarch64.tar.gz` |

Every package contains the filter, the PPD, the color tables, and the `dell1320c` command. The packages are built by the release workflow, or locally with `make pkg` and `make linux-packages` (see [Development](development.md#packaging)).

### macOS

The installer package is not signed with an Apple Developer ID, so when it was downloaded from the internet, Gatekeeper blocks it from opening in Finder. Install it from Terminal instead:

```sh
sudo installer -pkg Dell1320c-0.1.0.pkg -target /
```

To use the graphical installer, open the package once, then go to **System Settings → Privacy & Security** and click **Open Anyway**.

### Debian and Ubuntu

```sh
sudo apt install ./dell1320c_0.1.0-1_amd64.deb
```

`apt` also installs CUPS and cups-filters if they are missing. It recommends `snmp` and `curl`, which `dell1320c add` uses to read the printer's trays.

### Fedora and RHEL

```sh
sudo dnf install ./dell1320c-0.1.0-1.x86_64.rpm
```

### Other Linux distributions

The tarball holds the same files as the packages, under `usr/`, but it doesn't run the install script, so run `sudo dell1320c refresh` after upgrading. Install CUPS and cups-filters with your package manager first, then:

```sh
sudo tar -xzf dell1320c-0.1.0-linux-x86_64.tar.gz -C / --no-overwrite-dir
```

`--no-overwrite-dir` keeps the permissions and dates of existing system directories such as `/usr/bin`.

## Building from source

### Build requirements

The driver is written in C++23 and needs a compiler with `std::print` support: GCC 14 or newer on Linux, or Xcode or the Command Line Tools 15.3 or newer on macOS. Xcode 15.3 needs macOS 14 or newer, so building on a Mac needs macOS 14, but the driver it builds runs on macOS 13.3 or newer.

| System | Install the build tools with | Notes |
|---|---|---|
| macOS 14 or newer | `xcode-select --install` | CUPS and zlib come with macOS. |
| Debian 13 | `sudo apt install g++ make libcups2-dev zlib1g-dev` | |
| Ubuntu 24.04 | `sudo apt install g++-14 make libcups2-dev zlib1g-dev` | Build and install with `make CXX=g++-14` and `sudo make install CXX=g++-14`. |
| Fedora | `sudo dnf install gcc-c++ make cups-devel zlib-devel` | Fedora 40 or newer has GCC 14. |
| RHEL 9, AlmaLinux 9, Rocky Linux 9 | `sudo dnf install gcc-toolset-14-gcc-c++ make cups-devel zlib-devel` | Run `source /opt/rh/gcc-toolset-14/enable` before `make`. |

Debian 12 and Ubuntu 22.04 have no GCC 14, so use the packages there.

On Linux the driver also needs CUPS and cups-filters at run time. The `cups` package on Debian and Ubuntu installs both.

### Build and install

```sh
make
sudo make install
```

`make` builds into `build/`. On macOS it builds a universal filter that runs on Apple silicon and Intel Macs.

`make install` copies these files:

| File | macOS | Linux |
|---|---|---|
| Filter `rastertodell1320c` | `/Library/Printers/Dell1320c/` | The CUPS filter directory, usually `/usr/lib/cups/filter/` |
| Color tables | `/Library/Printers/Dell1320c/color/` | `/usr/local/share/dell1320c/color/` |
| PPD | `/Library/Printers/PPDs/Contents/Resources/Dell Color Laser 1320c.gz` | `/usr/local/share/ppd/dell1320c/dell1320c.ppd` |
| The `dell1320c` command | `/usr/local/bin/` | `/usr/local/bin/` |
| The `dell1320c` manual page | `/usr/local/share/man/man1/` | `/usr/local/share/man/man1/` |
| `LICENSE` and `NOTICE` | `/Library/Printers/Dell1320c/` | `/usr/local/share/doc/dell1320c/` |

On Linux, set `PREFIX` to install somewhere other than `/usr/local`, for example `make PREFIX=/usr && sudo make install PREFIX=/usr`. The color table location is built into the filter, so use the same `PREFIX` for both commands. `DESTDIR` is supported for staged installs.

## Upgrade

Install the new package over the old one. CUPS keeps its own copy of the PPD for each print queue, so the packages run `dell1320c refresh` after installing, which gives every queue that uses this driver the new PPD and keeps the options chosen for it. After upgrading a source install or a tarball install, run `sudo dell1320c refresh` yourself.

## Remove

On macOS, one command removes the print queues that use the driver, the driver files, and the `dell1320c` command itself. It works for both the package and a source install:

```sh
sudo dell1320c uninstall
```

On Linux, remove the print queues first. `dell1320c list` shows them; remove each one by name:

```sh
dell1320c list
sudo dell1320c remove Dell_Color_Laser_1320c
```

Then remove the driver:

```sh
sudo apt remove dell1320c    # Debian and Ubuntu
sudo dnf remove dell1320c    # Fedora and RHEL
sudo make uninstall          # source install, with the same PREFIX used to install
```

For a tarball install, delete its files:

```sh
sudo rm -f /usr/bin/dell1320c /usr/lib/cups/filter/rastertodell1320c /usr/share/man/man1/dell1320c.1.gz
sudo rm -rf /usr/share/dell1320c /usr/share/ppd/dell1320c /usr/share/doc/dell1320c
```
