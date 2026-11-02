# AGENTS.md

CUPS driver for the Dell Color Laser 1320c (a rebadged Fuji Xerox DocuPrint C525 A). One C++23 filter, `rastertodell1320c`, converts CUPS raster into the printer's PJL and HBPL data. Its output must stay byte-for-byte identical to the vendor driver's.

Read `docs/architecture.md` for how the code works and `docs/development.md` for every build target, test, and tool.

## Commands

```sh
make WERROR=1 check          # build, unit tests, golden tests
make format-check tidy lint  # clang-format, clang-tidy, shellcheck, cupstestppd, ruff and actionlint
make format                  # apply clang-format
make BUILD=/tmp/asan SANITIZE=1 ARCHS=arm64 check   # sanitizer build (macOS)
```

Run all of them before finishing a change. CI (`.github/workflows/ci.yml`) runs `make WERROR=1 check` on macOS (also with the sanitizers, and builds the installer), Debian 13, AlmaLinux 9 and Fedora, runs `make format-check tidy lint` on Ubuntu, and builds the Linux packages and runs `make package-test` on them. `make pkg` and `make linux-packages` build the installers; see "Packaging" in `docs/development.md`. Pin any new GitHub Action to a commit hash, with the version in a comment after it. Tools come from `uvx` (clang-format and clang-tidy 23.1.3); never run `python` or `pip` directly, use `uv run`.

## Rules

- **The golden tests are the specification.** All 76 cases in `test/golden/` must pass. If a change alters the printer data, it is wrong unless new vendor output has been recorded with `tools/golden/record.sh`, which needs a Mac with Rosetta and the Fuji Xerox driver.
- **Keep the vendor quirks.** The rules listed under "Matching the vendor driver exactly" in `docs/architecture.md` look arbitrary but are required. Don't simplify them.
- **Compressor changes:** after any change to `src/sq21.cpp`, also run `make tools` and then `uv run tools/sq21/compare.py build/sq21-encode`, which compares the compressor with the vendor's on random images. The unit tests only hold a few fixed vectors.
- **Style:** follow `.agents/skills/cpp-format/SKILL.md`, which also lists the `make format` and `make tidy` commands that check it. No code comments. Descriptive snake_case names, PascalCase types, UPPER_SNAKE_CASE constants, one variable per declaration, `const` on every local that never changes, 140 columns.
- **Platforms:** the code must build without warnings on Apple clang (macOS 13.3 deployment target, universal binary) and on GCC 14 (Debian 13, and AlmaLinux 9 with gcc-toolset-14). `std::format` and `std::print` are fine; anything newer in libc++ must be checked against the macOS 13.3 availability markings.
- **Errors:** problems with the input (the PPD, color tables, raster data) return `std::expected` with a message. Failures to write the output and running out of memory throw, and so do broken preconditions. `main` reports all of them as `ERROR:` lines for CUPS. When converting a page fails, it ends the printer job first, so the printer doesn't get half a job; only a failure to write the output itself can leave the job unfinished.

## Layout

| Path | Contents |
|---|---|
| `src/` | The filter, and `dell1320c.sh.in`, the queue setup command (POSIX sh) |
| `data/ppd/dell1320c.ppd.in` | The PPD, written by `tools/ppd/generate.py` from its own tables; regenerate it rather than editing it |
| `data/color/` | Color tables recorded from the vendor driver |
| `test/unit/` | Unit tests for each module, and the small test framework in `testing.hpp` |
| `test/golden/` | Recorded rasters and vendor output, and the runner |
| `packaging/` | The macOS installer and Linux package build scripts |
| `tools/` | Scripts that record golden output, color tables and the PPD; `tools/sq21/` has `sq21-encode`, a command-line front end to the compressor, and the comparison with the vendor encoder |
