---
name: cpp-format
description: Use when writing, editing or reviewing C++ code in this repository (src/, test/unit/ and tools/sq21/, files ending in .cpp and .hpp). Explains how the code must be laid out and named, in particular how to handle lines longer than the 140-column limit without leaving clang-format's continuation wraps, one variable per line, const locals, descriptive snake_case names with UPPER_SNAKE_CASE constants, fixing every violation found even outside the task, and which tools format and check the code (clang-format 23.1.3 and clang-tidy 23.1.3 through make, and CI).
---

# Formatting and naming C++ in this repository

The code is C++23. Its layout is set by `.clang-format` (based on LLVM style, 4-space indent, 140-column limit, braces on the same line, consecutive declarations aligned). clang-format decides spacing and alignment, so never align or space code by hand. It does not decide how statements are split, and that is the part this skill covers.

## Tools

| Tool | Command | What it does |
| :-- | :-- | :-- |
| clang-format 23.1.3 | `make format` | Formats every C++ file in place. |
| Formatting check | `make format-check` | Succeeds, with no messages from clang-format, when every C++ file is formatted. CI runs it. |
| clang-tidy 23.1.3 | `make tidy` | Writes `build/compile_commands.json`, then runs the checks in `.clang-tidy` over every C++ source and fails on any warning. The checks find local declarations that declare more than one variable, and many of the local variables and reference or pointer parameters that could be `const` but are not (see [Const locals](#const-locals) for the ones it misses). CI runs it. |
| clang-tidy fixes | `make compile-commands`, then `uvx clang-tidy@23.1.3 --fix -p build <files>` and `make format` | Applies clang-tidy's fixes to the given files, then formats them. |

Both tools run through `uvx`, so they need only `uv`. On macOS `make tidy` writes the SDK path and the deployment target into `build/compile_commands.json`, so clang-tidy finds the system headers.

## No continuation wraps at the column limit

When a line is longer than 140 columns, clang-format breaks it at whatever point fits and indents the rest as a continuation line. The result is hard to read and is not acceptable in this repository. Do not leave these wraps in code you write or touch:

```cpp
    for (std::uint32_t row = 0; row < header.cupsHeight && row < crop.top + crop.height && !g_cancelled.load(std::memory_order_relaxed);
         row++) {
        std::size_t crop_offset = std::size_t{crop.left} * RGB_CHANNELS,
                    crop_size = std::size_t{crop.width} * RGB_CHANNELS;
```

Instead, change the code so that each line fits. Which change to make depends on what the long line is.

### Declarations and assignments: split them into several statements

A declaration with several variables becomes one declaration per variable:

```cpp
    std::size_t crop_offset = std::size_t{crop.left} * RGB_CHANNELS;
    std::size_t crop_size = std::size_t{crop.width} * RGB_CHANNELS;
```

A long expression on the right of `=` becomes several statements, each naming one intermediate value:

```cpp
    float  extent_pixels = (high - low) * static_cast<float>(dots_per_inch(resolution));
    double exact = static_cast<double>(extent_pixels) / static_cast<double>(POINTS_PER_INCH);
```

Choose names that say what the value is. Do not use names such as `tmp`, `t1` or `part`.

### Conditions in `if`, `while` and `for`: name the parts or compute them first

Compute a loop bound before the loop:

```cpp
    std::uint32_t crop_end = crop.top + crop.height;
    for (std::uint32_t row = 0; row < header.cupsHeight; row++) {
```

Give a long condition named `bool` parts:

```cpp
    bool width_in_range = header.cupsWidth > 0 && header.cupsWidth <= sq21::MAX_LINE_WIDTH;
    bool whole_line = header.cupsBytesPerLine == std::uint64_t{header.cupsWidth} * RGB_CHANNELS;
    return rgb && eight_bit_chunky && width_in_range && whole_line;
```

Moving a value out of a loop does not cost performance. clang at `-O2` already loads such values once when nothing in the loop can change them, so do this for readability, not speed.

### Calls, signatures and braced lists: expand them

A function call, function declaration or definition, or braced initializer list that does not fit is expanded. The opening bracket ends its line, the contents go on the following lines indented one level, and the closing bracket goes on its own line at the indentation of the statement. clang-format produces this form by itself (`AlignAfterOpenBracket: BlockIndent`), so write the call on one line and let clang-format expand it:

```cpp
[[nodiscard]] std::vector<std::uint8_t> compress(
    ColorMode mode, std::span<const std::uint8_t> pixels, std::uint32_t width, Resolution resolution
);
```

```cpp
        return std::unexpected(
            std::format(
                "unsupported raster page: {} pixels wide, color space {}, {} bits per color, {} bits per pixel, color order {}, {} bytes per line",
                header.cupsWidth, static_cast<unsigned>(header.cupsColorSpace), header.cupsBitsPerColor, header.cupsBitsPerPixel,
                static_cast<unsigned>(header.cupsColorOrder), header.cupsBytesPerLine
            )
        );
```

If the call is itself the right side of a long assignment, such as `Crop crop = centered_crop(raster_size, printable_size);` grown too long, the expanded form is fine. If the line is long because the expression around the call is long, split it into statements as described above.

clang-format does not break string literals (`BreakStringLiterals: false`), so a long format string can stay on one line inside an expanded call.

A constructor's member initializer list that does not fit after the signature goes on its own line, starting with `:`, as clang-format places it. That is not a continuation wrap.

### How to find wraps before they happen

Before running clang-format, list the lines in the files you changed that are over the limit:

```sh
awk 'length > 140 { print FILENAME ":" FNR }' <files>
```

Every line listed must be either split into statements or be a call, signature or braced list that clang-format will expand. After formatting, read the diff, and treat any line that continues the previous line without a bracket that opens on the line above as a mistake to fix.

## One variable per line

Every declaration declares one variable, and every assignment assigns one variable, each on its own line. This applies everywhere: local variables, file-scope variables and constants, and struct and class fields.

```cpp
constexpr std::uint8_t CUSTOM_PAPER_CODE = 0xff;
constexpr std::uint8_t FALLBACK_PAPER_CODE = 0x02;

struct PixelSize {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

            pending_symbol = 0;
            after_partial_run = false;
```

Do not write any of these:

```cpp
constexpr std::uint8_t CUSTOM_PAPER_CODE = 0xff, FALLBACK_PAPER_CODE = 0x02;
std::uint32_t          width, height;
int                    pending_run = 0, pending_symbol = 0;
pending_run = pending_symbol = 0;
pending_symbol = 0, after_partial_run = false;
```

The exception is the header of a `for` loop, which may declare or step several loop variables, as in `for (int front = 0, back = count - 1; front < back; front++, back--)`.

What the tools check:

- `make tidy` finds local declarations with several variables, and clang-tidy's `--fix` splits them.
- Nothing checks file-scope variables, struct fields, chained assignments (`a = b = 0;`) or comma-separated assignments (`a = 0, b = 1;`). Write them one per line yourself.

`AlignConsecutiveDeclarations` aligns the names of consecutive declarations. A blank line ends an aligned group. Use blank lines to separate unrelated groups instead of adjusting spaces.

## Const locals

Declare every local variable `const` when the function never changes it after initialization, and pass reference and pointer parameters as `const` when the function only reads through them. `const` goes on the left of the type, as in `const std::size_t channels = channel_count(mode);`, and clang-format moves it there (`QualifierAlignment: Left`).

Leave a local without `const` when the function returns it by name or passes it to `std::move`. A `const` object can't be moved from, so it is copied wherever the compiler doesn't elide the copy.

`make tidy` finds many locals and parameters that could be `const`, and clang-tidy's `--fix` adds it. Run `make format` afterwards to move the `const` to the left. The check misses two kinds, so look for these yourself: locals whose type the standard library declares with a `using` declaration, which on macOS includes the `std::uint32_t` family but not `std::size_t`; and locals passed by reference to a function template such as `std::format` or `std::println`.

## Naming

Names use snake_case. camelCase is not used for any name this repository defines.

| Kind of name | Style | Example |
| :-- | :-- | :-- |
| Local variables, parameters, functions, struct and class fields, namespaces | snake_case | `page_number`, `load_settings`, `color_mode`, `sq21` |
| Global variables | snake_case with a `g_` prefix | `g_cancelled`, `g_failure_count` |
| Compile-time constants: `constexpr` and `const` values with a constant initializer at file, namespace or class scope, and enumerators | UPPER_SNAKE_CASE | `MAX_LINE_WIDTH`, `POINTS_PER_INCH`, `RGB_CHANNELS`, `DPI_600` |
| Macros | UPPER_SNAKE_CASE | `DELL1320C_COLOR_DIR` |
| Types: structs, classes, unions, enums, type aliases, template type parameters and concepts | PascalCase | `Settings`, `ColorTable`, `PageConverter`, `ColorMode` |

- Constants do not use the `k` prefix. Write `constexpr std::uint32_t MAX_LINE_WIDTH = 1u << 16;`, not `constexpr std::uint32_t kMaxWidth = 1u << 16;`.
- A `const` local whose value is computed at run time, such as `const std::size_t crop_size = ...;` inside a function, is an ordinary variable and stays snake_case.
- Names that come from outside the repository keep their original spelling: CUPS, zlib, POSIX and C library identifiers such as `cups_page_header2_t` and `cupsBytesPerLine`, and PPD option and choice names such as `FXDocumentType`.

### Names must say what they hold

Every name must tell a reader what the variable, field, parameter, constant or function is, so that nobody has to read the surrounding code or guess twice. Use whole English words, and keep the name as short as it can be while still being clear.

| Write | Not |
| :-- | :-- |
| `page_number`, `pixel`, `header`, `pixel_count` | `a`, `b`, `c`, `n` |
| `source_pixel`, `mapped_line` | `src`, `dst`, `sp`, `ml` |
| `left_margin`, `top_margin` | `m0`, `m1`, `margin1`, `margin2` |
| `header`, `record`, `compressed` | `tmp`, `data`, `val`, `res`, `obj`, `ptr`, `buf` |
| `MAX_LINE_WIDTH` | `MAX_W`, `kMaxWidth` |
| `page` | `current_page_that_is_being_converted` |

- No single-letter names, numbered names (`v1`, `tmp2`) or vague names (`tmp`, `data`, `value`, `result`, `info`, `stuff`) on their own. If the value is a result, name what it is the result of, such as `converted` or `compressed`.
- No abbreviations, unless the abbreviation is more common than the full word in this field: `dpi`, `ppd`, `rgb`, `id`, `utf8`. `ch` is not acceptable, because it could mean channel or character.
- Template type parameters follow the same rule: `Element` or `Callback`, not `T` or `F`.
- Two exceptions: `i` for a plain index in a short loop whose header shows what it counts, such as `for (std::size_t i = 0; i < pixels.size(); i++)`, and `x`, `y` and `z` for coordinates. Name nested loop indexes after what they count (`row`, `column`, `layer`), not `i` and `j`.
- A name that copies an external name, as described above, keeps its spelling even when it is short.

## Fix every violation you find

When you come across code that breaks a rule in this skill, fix it, even when the task did not ask for it and the code is outside the change you are making. This covers continuation wraps, several variables in one declaration or assignment, locals that could be `const`, names in the wrong style, and names that do not say what they hold. Make the fix in the branch you are working on; do not leave it for later or only mention it.

- Commit these fixes separately from the task's change, one kind of fix per commit.
- When you rename something, change every use. Search the whole repository, including `docs/`, `AGENTS.md` and the scripts, not only `src/`.
- After the fixes, run the whole checklist below.

## Checklist for a C or C++ change

1. Write lines that fit in 140 columns, one variable per line, `const` locals, with descriptive names in the styles above. Check the length with the `awk` command.
2. Fix any violation of these rules that you found in the code you read, in its own commit.
3. Run `make format`.
4. Read the diff for continuation wraps and for unrelated reformatting.
5. Run `make tidy` and fix any warning.
6. Run `make WERROR=1 check`, which builds the driver and runs the unit and golden tests. The golden tests must stay at 76 of 76.
