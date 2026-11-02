# Dell Color Laser 1320c driver

A CUPS printer driver for the Dell Color Laser 1320c, which is a rebadged Fuji Xerox DocuPrint C525 A. The vendor drivers for this printer are Intel only: the macOS driver needs Rosetta on Apple silicon Macs, and the Linux driver is a 32-bit x86 package, so neither runs natively on ARM. This driver is a native replacement that produces the same printer data as the vendor driver.

## Supported systems

| System | Versions | Processors |
|---|---|---|
| macOS | 13.3 (Ventura) or newer | Apple silicon and Intel |
| Linux | Debian 12, Ubuntu 22.04, RHEL 9.7, Fedora, and other distributions with CUPS 2 and glibc 2.35 or newer (the glibc 2.34 in RHEL 9.7 and later also works, because Red Hat added the one newer function the driver uses in that release) | x86_64 and ARM64 |

No Rosetta, Ghostscript, or Homebrew is needed on macOS.

I use the driver with my own printer on macOS 27. The Linux packages pass automated install and print tests on Debian 12 and 13, Ubuntu 22.04, 24.04 and 26.04, AlmaLinux 9, and Fedora, but I haven't had time to try them with a printer yet. If something doesn't work for you, please open an issue, and I'm happy to look into it.

## Features

- **Same output as the vendor driver.** For every paper size, tray, color mode, duplex mode, resolution, and copy option in the test suite, the printer data is byte-for-byte identical to the Fuji Xerox driver.
- **The vendor's color correction.** The color tables for each Color Correction setting (Standard, Photos, Presentations, Web Pages, Posters and Signs) and for black-and-white printing were recorded from the vendor driver.
- **One-command printer setup.** `dell1320c add <address>` creates the print queue with Tray 1 selected, so jobs don't go to the manual feed slot, and checks which trays the printer reports.
- **Every option of the vendor driver,** including collated copies, 300 and 600 dpi, paper types, custom paper sizes, envelopes, skip blank pages, 180° rotation, and toner saving.
- **Fast and light.** The driver streams each page line by line, so it needs about 60 MB of memory (about 160 MB for pages it turns 180°) and converts a color page in about 0.15 seconds on an Apple silicon Mac. Collated copies need more, because the compressed pages of the whole job are kept until every copy is written.

## Quick start

Download the package for your system from the repository's Releases page, then install it:

```sh
sudo installer -pkg Dell1320c-0.1.0.pkg -target /     # macOS
sudo apt install ./dell1320c_0.1.0-1_amd64.deb        # Debian and Ubuntu
sudo dnf install ./dell1320c-0.1.0-1.x86_64.rpm       # Fedora and RHEL
```

Then add the printer, with your printer's address in place of the example:

```sh
sudo dell1320c add 192.168.0.80 --default
```

[Installation](docs/installation.md) covers ARM Linux, other distributions, and building from source. [Printer setup](docs/printer-setup.md) covers USB printers.

## Documentation

- [Installation](docs/installation.md): packages, building from source, upgrading, and removing the driver.
- [Printer setup](docs/printer-setup.md): creating print queues, trays, print options, and troubleshooting.
- [How the driver works](docs/architecture.md): the print path, the printer data format, and the source layout.
- [Development](docs/development.md): building, testing, packaging, and recording vendor reference output.

## Credits

This project is a rewrite of [biosed/dell-1320c-cups-driver](https://github.com/biosed/dell-1320c-cups-driver), which reverse engineered the vendor's filter chain and its SQ21 compression. This version replaces that code with a single C++23 filter and adds the vendor color correction, exact vendor output, tests, and the setup command.

## License

The driver's code is released under the [MIT License](LICENSE). The color tables and the expected test output are derived from the Fuji Xerox driver and are not covered by it; [NOTICE](NOTICE) lists them.
