# How the driver works

The driver is one CUPS filter, `rastertodell1320c`, plus a PPD that tells CUPS to use it. CUPS turns each document into CUPS raster, an RGB image of every page that may be compressed, and the filter turns that into the printer's own format.

## The print path

```
 ┌─────────────┐  PDF   ┌──────────────────────────┐  CUPS raster   ┌───────────────────┐
 │ Application ├───────►│ CUPS rasterizer          ├───────────────►│ rastertodell1320c │
 └─────────────┘        │ macOS: cgpdftoraster     │  8-bit RGB,    └─────────┬─────────┘
                        │ Linux: cups-filters      │  one image                │
                        └──────────────────────────┘  per page                 │ PJL + HBPL
                                                                               ▼ printer data
                        ┌──────────────────────────┐                 ┌───────────────────┐
                        │ Dell 1320c               │◄────────────────┤ CUPS backend      │
                        └──────────────────────────┘  LPD, port 9100 │ lpd, socket, usb  │
                                                      or USB         └───────────────────┘
```

1. The application sends the document to CUPS, usually as PDF.
2. CUPS reads the PPD's `cupsFilter` line, which says the driver accepts `application/vnd.cups-raster`. CUPS therefore runs its own rasterizer first, at the resolution the job asks for.
3. `rastertodell1320c` reads the raster, the job options, and the PPD (from the `PPD` environment variable), and writes the printer data to standard output.
4. The backend named in the queue's device URI sends that data to the printer.

## Inside the filter

```
 job options + PPD ──► load_settings() ──► Settings
                                              │
 color table file ───► ColorTable::load() ────┤   (none for color with Color Correction Off)
                                              ▼
                                   JobWriter::begin()  ──► PJL job header
                                              │
              ┌───────────────────────────────┘
              ▼
   ┌─► next raster page? ──no──► JobWriter::end() ──► repeated sets for collated copies,
   │          │ yes                                     then the job trailer
   │          ▼
   │   PageConverter::convert(), one raster line at a time:
   │     read line ─► crop to the printable area ─► rotate? ─yes─► hold the page, replay
   │                                                  │ no         lines in reverse order
   │                                                  ▼
   │     blank check ─► color table (RGB to corrected RGB, or to gray) ─► SQ21 compressor
   │          │
   │          ▼
   │   blank page and Skip Blank Pages on? ──yes──┐
   │          │ no                                │
   │          ▼                                   │
   │   JobWriter::add_page() ──► page record + compressed data
   │          │                                   │
   └──────────┴───────────────────────────────────┘
```

- **Settings** (`settings.cpp`) marks the job options in the PPD with libcups and reads the result: paper size and margins, tray, color mode, color correction, duplex, copies, and so on. The printer's settings for each Color Correction choice come from a table in `settings.cpp`.
- **Color correction** (`color_table.cpp`) maps every RGB pixel through a table recorded from the vendor driver. Each table holds the output for all 16.7 million RGB colors: three bytes per color for the color tables, and one byte for the black-and-white tables. The files in `data/color/` are compressed with zlib and stored as differences between neighboring entries, which makes them about 30 MB in total instead of about 270 MB.
- **Page conversion** (`page_converter.cpp`) crops each page to the printable area and centers it, the same way the vendor driver does. In long-edge duplex, back pages are turned 180° when Rotate 180 Degrees is off and left upright when it is on; every other page is turned only when Rotate 180 Degrees is on. Only a turned page needs a whole uncompressed page in memory. The buffer for it is allocated by the first turned page and kept for the rest of the job. Compressed pages are small, but with collated copies every compressed page of the job is kept until the copies are written.
- **Compression** (`sq21.cpp`) encodes each line with the vendor's SQ21 method as soon as it arrives.
- **The job writer** (`hbpl.cpp`) writes the job header before the first page and each page as soon as it is compressed, so the printer can start before the last page is converted.

## The printer data

The printer expects a PJL job that switches to HBPL, Fuji Xerox's binary page language.

```
 ESC%-12345X @PJL JOB ...               job header: user, title, copies, tray, duplex,
 @PJL SET ...                           paper type, resolution, color settings
 @PJL ENTER LANGUAGE=HBPL
 ┌──────────────────────────────────┐
 │ document begin (12 bytes)        │
 │ page record ─► compressed page   │   one record per printed page, in order
 │ "SD"                             │
 │ ...                              │
 │ "B"                              │   end of the HBPL document
 └──────────────────────────────────┘
 ESC%-12345X @PJL COMMENT FXJOBINFO ... trailer: page count and a line per page
 @PJL EOJ
```

HBPL is made of records. Each record starts with a record type byte, followed by attributes. Each attribute is an attribute number, a value type, and the value, and all numbers are little-endian. `src/hbpl.cpp` names them in the `RecordType`, `ValueType` and `Attribute` enumerations.

| Record type | Byte | Contents |
|---|---|---|
| `BEGIN_DOCUMENT` | `0x41` | Three attributes whose meaning is unknown, always with the same values |
| `BEGIN_PAGE` | `0x43` | The page attributes below |
| `BEGIN_IMAGE`, `IMAGE_DATA` | `0x51`, `0x52` | The data length, a 24-byte band header, and the compressed page |
| `END_IMAGE`, `END_PAGE` | `0x53`, `0x44` | Nothing. Together they are the `SD` after each page. |
| `END_DOCUMENT` | `0x42` | Nothing. This is the `B` before the trailer. |

| Page attribute | Number | Value |
|---|---|---|
| `LONG_EDGE_FEED` | `0x91` | 1 when the paper is at least as wide as it is long |
| `TRAY` | `0x92` | 0 for automatic, 1 for the bypass tray, 2 for Tray 1, 3 for Tray 2 |
| `PAPER_SIZE` | `0x94` | Paper size code, or `0xff` for a custom size |
| `CUSTOM_PAPER_SIZE` | `0x95` | Custom paper width and length in pixels |
| `MARGINS` | `0x97` | Left and top margins in pixels |
| `DUPLEX_SIDE` | `0x98` | 1 for the front, 2 for the back, counted within each collated set |
| `PAGE_NUMBER` | `0x99` | Page number, counted across the whole job |
| `IMAGE_SIZE`, `IMAGE_SIZE_COPY` | `0x9a`, `0xa2` | Image width, padded to a multiple of 8, and height. The vendor writes the same size twice. |
| `COMPRESSION`, `BITS_PER_PIXEL` | `0x9d`, `0xa0` | `0x16` and 32 for color, `0x11` and 8 for gray |
| `RESOLUTION` | `0xa1` | 3 at 300 dpi, 0 at 600 dpi |
| `UNKNOWN_93`, `UNKNOWN_96`, `UNKNOWN_9B`, `UNKNOWN_9C`, `UNKNOWN_9E`, `UNKNOWN_9F`, `UNKNOWN_A3` | | Meaning unknown. The vendor driver always writes the same values. |

The band header holds the compressor's starting settings: the predictor order (`0x00043210`, meaning predictors 0 to 4 in order) and the longest run one code can hold (17057 pixels).

The record structure and the meanings of `0x94`, `0x95`, `0x99`, `0x9a`, `0x9d`, `0xa2` and the data length come from `hbpldecode.c` in the [foo2zjs](https://github.com/OpenPrinting/foo2zjs) project, which decodes the format of related printers. One difference: `hbpldecode.c` reads `0x95` in units of 0.1 mm, while this printer's vendor driver writes pixels. The other meanings come from comparing the vendor driver's output for different settings.

## SQ21 compression

SQ21 is a lossy, line-based method. Color pixels are 32-bit words with one padding byte and three color bytes. Gray pixels are single bytes. For each pixel the encoder tries five predictors: the pixel to the left, the pixel above, the pixels above-left and above-right, and one more that differs between color and gray.

- **Runs.** If one or more predictors match the next pixels exactly, the encoder writes the longest run. The predictors are kept in most-recently-used order, so the common cases get the shortest codes.
- **Near matches.** When no predictor matches exactly, a pixel that is within a small tolerance of a predictor is replaced by that predictor's value. The tolerance depends on the resolution and on how long the current run is. The difference is kept and spread into the next pixels' predictions, which works like error diffusion.
- **Literals.** Any other pixel is written as the difference from the pixel to its left, with a fixed Huffman code.
- **Code tables.** The encoder has two code tables. While it encodes a line, it keeps a count that goes down by one for each literal and up by one for each run that starts with the first predictor and for each maximum-length piece of a run. At the end of the line, a count above 8 selects the first table for the next line, and a count below -8 selects the second; otherwise the table stays the same.
- **Line-end markers.** A marker value written just past the end of each line stops runs there. Its bits are chosen to differ from every predictor's value at that position.

The names in `src/sq21.cpp` describe what each part does. The original project's names are below. The tables (`SQ21_HID`, `SQ21_HPE0`, `SQ21_HRN0`) and the functions `hire_sentinel`, `fire_sentinel` and `sq21_neares_func_lowres_enhanced` also appear by those names in the vendor driver's symbol table.

| This code | Original name |
|---|---|
| `near_match_tolerance` | `sq21_neares_func_lowres_enhanced` |
| `code_table`, `SYMBOL_CODES`, `update_code_table` | `hid`, `HID`, `update_hid` |
| `LITERAL_DELTA_CODES` | `HPE0` |
| `SHORT_RUN_CODES`, `LONG_RUN_GROUP_STARTS`, `LONG_RUN_EXTRA_BITS` | `HRN0` |
| `place_line_end_marker`, `CLEARED_MARKER_BYTE` | `hire_sentinel`, `fire_sentinel` |
| `far_predictor_offset` | `zoffset` |

## Matching the vendor driver exactly

The goal is printer data that is byte-for-byte identical to the vendor driver's, so several rules in the code copy vendor behavior that looks arbitrary. They were found by comparing output and by reading the vendor's disassembly. Don't simplify them without rerunning the golden tests:

- Printable widths and heights are computed by multiplying in single precision, dividing by 72 in double precision, and rounding half up.
- Margins are written only when the image and the margin together fit on the paper.
- In color mode the error spread into a prediction is a byte-by-byte average of four neighbors, built from three nested averages.
- A pixel written as a literal right after a failed run search is not adjusted by the spread error.
- The near-match state carries over from pixel to pixel within a line and is not cleared by literals.
- Each line-end marker bit is the inverse of the matching bit of the predictor's value.

## Source layout

| Path | Contents |
|---|---|
| `src/rastertodell1320c.cpp` | The filter's `main`: arguments, signals, and the page loop |
| `src/settings.*` | Reads job options and the PPD into `Settings` |
| `src/raster_reader.*` | Reads CUPS raster pages and lines |
| `src/file_descriptor.*` | Closes the input file automatically |
| `src/page_converter.*` | Turns one raster page into one compressed page, including the crop and printable-area rules |
| `src/color_table.*` | Loading and applying the color correction tables |
| `src/sq21.*` | The SQ21 compressor |
| `src/hbpl.*` | Writes the PJL and HBPL job |
| `src/dell1320c.sh.in` | The `dell1320c` command, a POSIX shell script |
| `data/ppd/dell1320c.ppd.in` | The PPD, generated by `tools/ppd/generate.py` |
| `data/color/` | The color correction tables |
| `test/` | Unit tests, golden tests, and test documents. See [Development](development.md). |
| `tools/` | Scripts that record the golden output and color tables from the vendor driver (`golden/`, `color-tables/`), write the PPD (`ppd/`), compare the compressor with the vendor's (`sq21/`), and write the compilation database for clang-tidy (`dev/`) |
