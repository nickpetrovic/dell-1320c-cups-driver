# Printer setup

Install the driver first, as described in [Installation](installation.md). Then create a print queue with the `dell1320c` command.

## Add a network printer

CUPS must be running. The `dell1320c` command stops with an error if it isn't. `man dell1320c` describes every command and option.

```sh
sudo dell1320c add 192.168.0.80 --default
```

This creates a queue named `Dell_Color_Laser_1320c` that prints to the printer over LPD. It selects the 250-sheet Tray 1 and a default paper size based on your region: Letter in the United States, Canada, Mexico, and the other countries that use it, and A4 elsewhere. `--default` also makes it the system's default printer.

Before creating the queue, the command asks the printer which paper trays it has, over SNMP (`snmpwalk`) or through the printer's web page (`curl`). If the printer reports the optional second tray, the queue is set up with **Paper Trays** set to **Tray 1 (250 Sheets) and Tray 2 (500 Sheets)**, so Tray 2 can be chosen in the print dialog. Tray 1 stays the default either way. The paper size must be one the driver supports; `dell1320c add` lists them if you name another.

| Option | What it does |
|---|---|
| `--name <name>` | Queue name. The default is `Dell_Color_Laser_1320c`. |
| `--description <text>` | Name shown in print dialogs. The default is `Dell Color Laser 1320c`. |
| `--location <text>` | Location shown in print dialogs. |
| `--paper <size>` | Default paper size, such as `Letter` or `A4`. |
| `--protocol lpd\|socket` | `lpd` (the default) or `socket`, which prints to port 9100. |
| `--default` | Makes this the default printer. |
| `--shared` | Shares the printer on the network. Queues are not shared by default. |

## Add a USB printer

Find the printer's device URI, then pass the URI instead of an address:

```sh
sudo lpinfo -v | grep -i dell
sudo dell1320c add 'usb://Dell/Color%20Laser%201320c?serial=XXXXXXXX'
```

## Other commands

| Command | What it does |
|---|---|
| `dell1320c list` | Lists the queues that use this driver and where they print. |
| `dell1320c trays <address>` | Shows the paper trays the printer reports. |
| `sudo dell1320c remove [name]` | Deletes a queue that uses this driver. Without a name it deletes `Dell_Color_Laser_1320c`. It refuses to delete queues that use other drivers. |
| `sudo dell1320c refresh` | Gives every queue that uses this driver the installed PPD, keeping the options chosen for each queue. The packages run it after every install and upgrade. |
| `sudo dell1320c uninstall` | Removes the driver and its queues (macOS only). |
| `dell1320c version` | Shows the driver version. |

## Adding the printer in System Settings

On macOS you can also add the printer in **System Settings → Printers & Scanners** and choose **Dell Color Laser 1320c** as the driver. Afterwards, open the printer's **Options & Supplies** and check that **Paper Trays** is **Tray 1 (250 Sheets)** (or **Tray 1 (250 Sheets) and Tray 2 (500 Sheets)** if you have the second tray) and **Paper Source** is **Tray 1 (250 Sheets)**. `dell1320c add` sets both for you.

## Paper trays

The printer's built-in drawer is **Tray 1 (250 Sheets)** in the driver. The PPD only offers that tray when **Paper Trays** includes **Tray 1 (250 Sheets)**.

If the printer's manual feed light blinks when a job arrives and nothing prints, the job was sent to the manual feed slot (**Bypass Tray**) and the printer is waiting for paper there. Set **Paper Source** back to **Tray 1 (250 Sheets)**.

## Print options

Print dialogs show these options under the names in the first column. Print dialogs and other IPP clients that set the standard color and quality options also work: black-and-white printing selects **Color Mode** Black and White, and draft quality selects 300 dpi, through presets in the PPD. The second column has the name to use with `lp -o`, for example `lp -o FXDocumentType=Photo photo.pdf`.

| Option | `lp` name | Choices |
|---|---|---|
| Paper Source | `FXInputSlot` | `1stTray-S` (Tray 1, the default), `BypassTray`, `AutoSelect` (Automatic), and the 500-sheet trays |
| Color Mode | `FXColorMode` | `Color` (the default) or `Black` (Black and White) |
| Resolution | `FXOutputMode` | `600x600` (the default) or `300x300` (faster) |
| Color Correction | `FXDocumentType` | `Normal` (Standard, the default), `Photo`, `Presentation`, `Web` (Web Pages), `POP` (Posters and Signs), or `Off` |
| Two-Sided | `Duplex` | `None` (the default), `DuplexNoTumble` (long-edge binding), or `DuplexTumble` (short-edge binding) |
| Collate Copies | `Collate` | `False` (the default) or `True` |
| Bypass Tray Paper Type | `MediaType` | `Default` (Printer Default, the default), or plain, bond, postcard, envelope, heavyweight, coated, labels, or transparency. The `Back` choices print the second side of a sheet. |
| Paper Type for Automatic Tray | `FXPreferredMediaType` | `Default` (Any, the default), `Stationary` (Plain), `FinePaper` (Bond), or `CoatingPaper1` (Coated 1). Only used with Paper Source Automatic. |
| Substitute Tray | `FXSubPaperSelection` | `Policy0` (Printer Default, the default), `Policy2` (Off, Show a Message), or `SMH` (Bypass Tray) |
| Skip Blank Pages | `FXSkip` | `True` leaves out pages with nothing on them |
| Rotate 180 Degrees | `FXTurnPage` | `True` turns every page upside down |
| Toner Saver | `FXTonerSaver` | `True` saves toner |

## Troubleshooting

- **A job fails or stays in the queue.** Run `lpstat -t` to see the queue and job state. The driver reports problems as `ERROR:` lines in the CUPS error log, which is usually `/var/log/cups/error_log`. On Linux systems where CUPS logs to the system journal, use `journalctl -u cups`.
- **"unable to read color table".** The color tables are missing or were installed with a different `PREFIX` than the filter was built with. Reinstall with the same `PREFIX` for `make` and `make install`.
- **Pages print on the wrong paper size.** The queue's default size comes from your region. Set another one with `sudo lpadmin -p Dell_Color_Laser_1320c -o PageSize=A4`, or recreate the queue with `--paper`.
