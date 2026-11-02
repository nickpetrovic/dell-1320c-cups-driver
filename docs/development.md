# Development

This page covers building, testing, and the tools that record reference output from the vendor driver. [How the driver works](architecture.md) explains the code itself.

## Tools

Besides the build requirements in [Installation](installation.md), development uses:

- [uv](https://docs.astral.sh/uv/), which runs clang-format, clang-tidy, and the Python scripts in `tools/` without installing them.
- `xz` and `cmp` (from `diffutils`), for the golden tests. macOS has `cmp` but not `xz`; install `xz` with Homebrew or MacPorts.
- Docker, for building and testing the Linux packages.
- `shellcheck`, for `make lint`.

## Make targets

| Target | What it does |
|---|---|
| `make` | Builds the filter, the PPD, the `dell1320c` command, and its manual page into `build/` |
| `make check` | Runs the unit tests and the golden tests |
| `make unit-test` | Runs the unit tests in `test/unit/` |
| `make golden-test` | Compares the filter's output with the recorded vendor output |
| `make tools` | Builds `build/sq21-encode`, a command-line front end to the compressor |
| `make format` | Formats the C++ sources with clang-format 23.1.3 |
| `make format-check` | Fails if any C++ file is not formatted |
| `make tidy` | Runs clang-tidy 23.1.3 with the checks in `.clang-tidy` |
| `make lint` | Runs shellcheck on the shell scripts, checks that the PPD matches `tools/ppd/generate.py`, runs `cupstestppd` on the PPD, ruff on the Python tools, and actionlint on the workflows |
| `make pkg` | Builds the macOS installer package into `build/dist/` |
| `make linux-packages` | Builds the `.deb`, `.rpm`, and tarball packages into `build/dist/`, using Docker |
| `make package-test` | Installs each Linux package in `build/dist/` on several distributions and prints through CUPS, using Docker |
| `make clean` | Deletes `build/`, including the packages in `build/dist/` |

Build options:

| Variable | Effect |
|---|---|
| `WERROR=1` | Treats compiler warnings as errors |
| `SANITIZE=1` | Builds with AddressSanitizer and UndefinedBehaviorSanitizer |
| `STATIC_RUNTIME=1` | Links the C++ runtime into the filter, so it runs on Linux systems with an older libstdc++ |
| `ARCHS=arm64` | On macOS, builds for one architecture instead of both |
| `CXX=g++-14` | Chooses the compiler |
| `BUILD=<directory>` | Builds somewhere other than `build/`, for example to keep a sanitizer build next to the normal one |

`build/build-config` records the compiler, flags, and install paths of the last build. Changing any of them, for example `PREFIX` or `WERROR=1`, rebuilds everything that depends on them.

Every build is hardened: the stack protector, the standard library's bounds-checking assertions (`_GLIBCXX_ASSERTIONS` for libstdc++, the fast hardening mode for libc++, or the hardened mode on the libc++ 17 in Xcode 15), and on Linux also `_FORTIFY_SOURCE=3`, a position-independent executable, full RELRO, and stack clash protection. The filter reads data sent by print clients, so these stay on for releases. On an Apple silicon Mac they make no measurable difference: a Letter color page from the golden tests takes about 0.15 s with and without them.

Before sending a change, run:

```sh
make WERROR=1 check
make format-check tidy lint
```

## Packaging

`make pkg` runs `packaging/macos/build.sh` on a Mac. It installs the driver into a staging directory, packs it with `cpio`, `mkbom`, and `xar`, and wraps it with `productbuild` into `build/dist/Dell1320c-<version>.pkg` (under `BUILD` when it is set). The script builds the package parts itself instead of using `pkgbuild`, because files built on a Mac can carry extended attributes such as `com.apple.provenance`, and `pkgbuild` adds a `._` file to the package for every file that has one. The `cpio` archive leaves them out. The installer runs `dell1320c refresh` afterwards, and its last page explains how to add the printer.

`make linux-packages` runs `packaging/linux/build.sh`, which needs Docker. For each architecture it:

1. Builds the filter in an AlmaLinux 9 container with GCC 14 and `STATIC_RUNTIME=1`. Building against an older glibc keeps the filter compatible with older distributions. The result needs glibc 2.35, because the static C++ runtime calls `_dl_find_object`, which Red Hat added to the glibc 2.34 of RHEL 9.7. That covers Debian 12, Ubuntu 22.04, RHEL 9.7, and everything newer.
2. Installs it with `PREFIX=/usr` into a staging directory and strips the filter.
3. Builds the `.deb` and `.rpm` with [nFPM](https://nfpm.goreleaser.com/) from `packaging/linux/nfpm.yaml`, and packs the same files into a tarball.

It builds both `amd64` and `arm64` by default. Set `ARCHITECTURES=amd64` to build one; on an ARM Mac, `amd64` runs under emulation and takes several minutes.

### Package tests

`make package-test` runs `test/packages/run.sh`, which installs every Linux package in `build/dist/` in a fresh container and runs `test/packages/print-test.sh` there:

| Package | Distributions |
|---|---|
| `.deb` | Debian 12 and 13, Ubuntu 22.04, 24.04, and 26.04 |
| `.rpm` | AlmaLinux 9 and the latest Fedora |
| Tarball | Debian 13 |

In each container the test starts CUPS and creates a queue with `dell1320c add` that prints to a listener on port 9100. It then prints the 3-page `test/documents/multi.pdf` and checks that the listener received a complete job with the right page count and color settings. It prints the same document again with the IPP options `print-color-mode=monochrome` and `print-quality=3` and checks that the job is black and white at 300 dpi. Last, it changes the queue's paper size and toner saver setting, and then its paper size to a custom size, and checks that `dell1320c refresh` keeps them. Set `DEB_IMAGES`, `RPM_IMAGES`, or `TARBALL_IMAGES` to test other distributions.

## Continuous integration

`.github/workflows/ci.yml` runs on every push to `main` and on pull requests:

- **macOS:** `make WERROR=1 check`, the same tests with the sanitizers, and `make pkg`.
- **Linux:** `make WERROR=1 check` on Debian 13, AlmaLinux 9, and the latest Fedora.
- **Linux packages:** `make linux-packages` and `make package-test`, on native x86_64 and ARM runners.
- **Style:** `make format-check`, `make tidy`, and `make lint`.

`.github/workflows/release.yml` runs when a `v*` tag is pushed. It checks that the tag matches `VERSION` and that `CHANGELOG.md` has an entry for that version that is not marked `(unreleased)`, runs the macOS tests and builds the macOS installer, builds the Linux packages and runs the package tests on them, and publishes all of them as a GitHub release. Actions are pinned to commit hashes, with the version in a comment after each hash.

## Code style

The sources are C++23. Formatting comes from `.clang-format`, and naming and line-splitting rules are in `.agents/skills/cpp-format/SKILL.md`. In short: snake_case names written out in full, PascalCase types, UPPER_SNAKE_CASE constants, one variable per declaration, no lines over 140 columns, and no code comments.

## Unit tests

`test/unit/` has a test file for each source module except `rastertodell1320c.cpp` (whose page loop the golden tests cover), with `raster_reader_test.cpp` also covering `file_descriptor`. A small test runner in `main.cpp` and the checks in `testing.hpp` replace a test framework. The tests need the generated PPD and the color tables, which `make unit-test` passes to them in the `DELL1320C_TEST_PPD` and `DELL1320C_COLOR_DIR` environment variables.

The compressor tests include outputs recorded from the vendor's encoder for small images, so a change to `sq21.cpp` that alters the output fails there first.

## Golden tests

The golden tests run the filter on recorded CUPS raster and compare its output with what the vendor driver produced for the same raster and options. All 76 cases must match byte for byte.

| File | Contents |
|---|---|
| `test/golden/cases.tsv` | One case per line: name, document, copies, and job options |
| `test/golden/index.tsv` | The same cases with the raster each one uses, written by the recorder |
| `test/golden/rasters/` | CUPS raster from macOS's `cgpdftoraster`, compressed with xz |
| `test/golden/expected/` | The vendor driver's output for each case, compressed with xz |
| `test/golden/run.sh` | The test runner |

After the 76 cases, `run.sh` also runs the filter on a raster cut off in the middle of a page and checks that it reports an error and still ends the printer job with `@PJL EOJ`.

Before comparing, `run.sh` removes the date and time lines and the computer's address, and blanks the computer's name, because those differ between runs. The address line is missing on a computer without a network address, so it is removed rather than blanked. A case listed in a `test/golden/known-differences` file is reported as an expected failure instead of failing the run. There is no such file now, because every case matches.

### Recording new cases

Recording needs a Mac with Rosetta and the Fuji Xerox driver for the DocuPrint C525 A (`FX DocuPrint C525 A-AP`) installed. Add a line to `cases.tsv`, then run:

```sh
tools/golden/record.sh
```

It rasterizes each document with `cgpdftoraster`, runs the vendor's filters on the result, and rewrites `rasters/`, `expected/`, and `index.tsv`. The `photo.pdf` and `noise.pdf` documents are generated while recording: the first from a macOS desktop picture, the second by `tools/golden/make_noise.py`.

## Color tables

The tables in `data/color/` were recorded from the vendor driver's color step. To record them again, on the same kind of Mac as above:

```sh
tools/color-tables/record.sh
```

It runs a raster with every possible RGB color through the vendor's `FXM_CC` once for each Color Correction setting and for black-and-white, and writes the results to `data/color/`.

## The PPD

`data/ppd/dell1320c.ppd.in` is generated by `tools/ppd/generate.py`, which holds the paper sizes, trays, paper types, and options in its own tables, along with the rules for which of them can be combined. To change the PPD, change the script and run it:

```sh
uv run tools/ppd/generate.py data/ppd/dell1320c.ppd.in
```

`make lint` fails when the committed PPD differs from the script's output. The option and choice names, such as `FXDocumentType` and `1stTray-S`, are the vendor driver's, so that the golden test cases, which were recorded with the vendor driver, use the same options, and so that existing queues keep their settings.

`make` fills in the filter path and the version from `VERSION`.

## Running the filter by hand

```sh
PPD=build/dell1320c.ppd DELL1320C_COLOR_DIR=data/color \
    build/rastertodell1320c 1 user title 1 "PageSize=Letter FXDocumentType=Photo" page.ras > page.prn
```

The arguments are the standard CUPS filter arguments: job ID, user, title, copies, options, and an optional input file. Without the file, the filter reads standard input.

| Environment variable | Effect |
|---|---|
| `PPD` | The PPD to read. CUPS always sets it. |
| `DELL1320C_COLOR_DIR` | Reads the color tables from this directory instead of the installed ones |
| `DELL1320C_DUMP_DIR` | Also writes each page after color correction to this directory as raw pixels, named `page-<number>-<width>x<height>x<channels>.raw` |

`build/sq21-encode` compresses raw pixels from standard input, which is useful for comparing the compressor with the vendor's:

```sh
build/sq21-encode rgb 5100 600 < pixels.raw > compressed.bin
```

`tools/sq21/compare.py` does that comparison on random images. It needs the Fuji Xerox driver installed, compresses each image with both encoders, and saves any image whose output differs in a new directory under the system's temporary directory, which it prints:

```sh
make tools
uv run tools/sq21/compare.py build/sq21-encode --trials 1000
```
