# /// script
# requires-python = ">=3.10"
# dependencies = []
# ///
import sys
from dataclasses import dataclass, field
from pathlib import Path

MARGIN = 11.28
HARDWARE_MARGIN = 11
CUSTOM_WIDTH = (255, 612)
CUSTOM_LENGTH = (396, 1008)

ALL_TRAYS = ("AutoSelect", "1stTray-S", "1stTray-H", "2ndTray-H", "BypassTray")
STANDARD_TRAY_SIZES = ("1stTray-S", "BypassTray")
BYPASS_ONLY = ("BypassTray",)
DUPLEX_CHOICES = ("DuplexNoTumble", "DuplexTumble")


@dataclass(frozen=True)
class Choice:
    name: str
    label: str
    code: str = ""


@dataclass(frozen=True)
class Option:
    keyword: str
    label: str
    order: float
    default: str
    choices: list[Choice]


@dataclass(frozen=True)
class Paper:
    name: str
    label: str
    width: int
    length: int
    trays: tuple[str, ...]
    duplex: bool
    media: tuple[str, ...] | None = None


@dataclass(frozen=True)
class MediaType:
    name: str
    label: str
    duplex: bool
    back_label: str | None = None


@dataclass
class Constraints:
    lines: list[str] = field(default_factory=list)

    def add(self, first: tuple[str, str], second: tuple[str, str]) -> None:
        self.lines.append(f"*UIConstraints: *{first[0]} {first[1]} *{second[0]} {second[1]}")
        self.lines.append(f"*UIConstraints: *{second[0]} {second[1]} *{first[0]} {first[1]}")


MEDIA_TYPES = [
    MediaType("Stationary", "Plain", duplex=True, back_label="Plain, Second Side"),
    MediaType("FinePaper", "Bond", duplex=True, back_label="Bond, Second Side"),
    MediaType("Postcard", "Postcard", duplex=False, back_label="Postcard, Second Side"),
    MediaType("Envelope", "Envelope", duplex=False),
    MediaType("ThickPaper", "Heavyweight 1 (106-163 g/m2)", duplex=False, back_label="Heavyweight 1, Second Side"),
    MediaType("ThickPaper2", "Heavyweight 2 (164-216 g/m2)", duplex=False, back_label="Heavyweight 2, Second Side"),
    MediaType("Transparency", "Transparency", duplex=False),
    MediaType("LabelPaper", "Labels", duplex=False),
    MediaType("CoatingPaper1", "Coated 1 (60-105 g/m2)", duplex=True, back_label="Coated 1, Second Side"),
    MediaType("CoatingPaper2", "Coated 2 (106-163 g/m2)", duplex=False, back_label="Coated 2, Second Side"),
    MediaType("CoatingPaper3", "Coated 3 (164-216 g/m2)", duplex=False, back_label="Coated 3, Second Side"),
]

HEAVY_MEDIA = ("ThickPaper", "ThickPaper2", "CoatingPaper2", "CoatingPaper3")
POSTCARD_MEDIA = ("Postcard", *HEAVY_MEDIA)
ENVELOPE_MEDIA = ("Envelope", *HEAVY_MEDIA)

PAPERS = [
    Paper("Letter", "US Letter", 612, 792, ALL_TRAYS, duplex=True),
    Paper("Legal", "US Legal", 612, 1008, BYPASS_ONLY, duplex=True),
    Paper("Executive", "Executive", 522, 756, STANDARD_TRAY_SIZES, duplex=True),
    Paper("FanFoldGermanLegal", "Folio (8.5 x 13 in)", 612, 936, BYPASS_ONLY, duplex=True),
    Paper("A4", "A4", 595, 842, ALL_TRAYS, duplex=True),
    Paper("A5", "A5", 421, 595, STANDARD_TRAY_SIZES, duplex=False),
    Paper("B5", "B5 (JIS)", 516, 729, STANDARD_TRAY_SIZES, duplex=True),
    Paper("Env10", "Envelope #10", 297, 684, BYPASS_ONLY, duplex=False, media=ENVELOPE_MEDIA),
    Paper("EnvMonarch", "Envelope Monarch", 279, 540, BYPASS_ONLY, duplex=False, media=ENVELOPE_MEDIA),
    Paper("FXEnvMonarchRotated", "Envelope Monarch, Long Edge First", 540, 279, BYPASS_ONLY, duplex=False, media=ENVELOPE_MEDIA),
    Paper("EnvDL", "Envelope DL", 312, 624, BYPASS_ONLY, duplex=False, media=ENVELOPE_MEDIA),
    Paper("FXEnvDLRotated", "Envelope DL, Long Edge First", 624, 312, BYPASS_ONLY, duplex=False, media=ENVELOPE_MEDIA),
    Paper("EnvC5", "Envelope C5", 459, 649, BYPASS_ONLY, duplex=False, media=ENVELOPE_MEDIA),
    Paper("FXEnvYou2Rotated", "Envelope C6, Long Edge First", 459, 323, BYPASS_ONLY, duplex=False, media=ENVELOPE_MEDIA),
    Paper("FXEnvYou3Rotated", "Envelope Youkei 3, Long Edge First", 420, 278, BYPASS_ONLY, duplex=False, media=ENVELOPE_MEDIA),
    Paper("EnvChou3", "Envelope Choukei 3", 340, 666, BYPASS_ONLY, duplex=False, media=ENVELOPE_MEDIA),
    Paper("FXPostcard", "Postcard (Japan)", 283, 420, BYPASS_ONLY, duplex=False, media=POSTCARD_MEDIA),
]

TRAY_MODULES = [
    Choice("None", "None"),
    Choice("1Tray-S", "Tray 1 (250 Sheets)"),
    Choice("1Tray-H", "Tray 1 (500 Sheets)"),
    Choice("1+2Tray-SH", "Tray 1 (250 Sheets) and Tray 2 (500 Sheets)"),
]

TRAY_MODULES_FOR_SLOT = {
    "AutoSelect": ("1Tray-S", "1Tray-H", "1+2Tray-SH"),
    "1stTray-S": ("1Tray-S", "1+2Tray-SH"),
    "1stTray-H": ("1Tray-H",),
    "2ndTray-H": ("1+2Tray-SH",),
}

PRESETS = [
    ("Color_Draft/Color, Draft", "Color", "300x300", "color", "low"),
    ("Color_Normal/Color", "Color", "600x600", "color", "mid"),
    ("Color_Best/Color, Best", "Color", "600x600", "color", "high"),
    ("Gray_Draft/Black and White, Draft", "Black", "300x300", "monochrome", "low"),
    ("Gray_Normal/Black and White", "Black", "600x600", "monochrome", "mid"),
    ("Gray_Best/Black and White, Best", "Black", "600x600", "monochrome", "high"),
]

HEADER = [
    '*PPD-Adobe: "4.3"',
    '*FormatVersion: "4.3"',
    '*FileVersion: "@VERSION@"',
    "*LanguageVersion: English",
    "*LanguageEncoding: ISOLatin1",
    '*PCFileName: "DELL1320.PPD"',
    '*Product: "(Dell Color Laser 1320c)"',
    '*Manufacturer: "Dell"',
    '*ModelName: "Dell Color Laser 1320c"',
    '*ShortNickName: "Dell Color Laser 1320c"',
    '*NickName: "Dell Color Laser 1320c"',
    '*PSVersion: "(3010.000) 0"',
    '*LanguageLevel: "3"',
    "*ColorDevice: True",
    "*DefaultColorSpace: CMYK",
    "*FileSystem: False",
    "*TTRasterizer: Type42",
    "*LandscapeOrientation: Minus90",
    '*1284DeviceID: "MFG:Dell;MDL:Color Laser 1320c;CMD:PJL,RASTER,DOWNLOAD,HBPL,NEXTC;CLS:PRINTER;"',
    "*cupsVersion: 1.2",
    '*cupsFilter: "application/vnd.cups-raster 0 @CUPS_FILTER@"',
    "*cupsManualCopies: False",
    "*cupsSingleFile: True",
    "*cupsSNMPSupplies: False",
    "*APSupportsCustomColorMatching: True",
    "*DefaultResolution: 600dpi",
]

RASTER_FORMAT = "<</cupsColorSpace 1/cupsColorOrder 0/cupsBitsPerColor 8>>setpagedevice"


def points(value: float) -> str:
    return f"{value:.2f}".rstrip("0").rstrip(".")


def page_size_code(paper: Paper) -> str:
    return f"<</PageSize[{paper.width} {paper.length}]/ImagingBBox null>>setpagedevice"


def option_lines(option: Option) -> list[str]:
    lines = [
        f"*OpenUI *{option.keyword}/{option.label}: PickOne",
        f"*OrderDependency: {option.order} AnySetup *{option.keyword}",
        f"*Default{option.keyword}: {option.default}",
    ]

    for choice in option.choices:
        lines.append(f'*{option.keyword} {choice.name}/{choice.label}: "{choice.code}"')

    lines.append(f"*CloseUI: *{option.keyword}")
    lines.append("")
    return lines


def paper_option(keyword: str, order: float) -> Option:
    choices = [Choice(paper.name, paper.label, page_size_code(paper)) for paper in PAPERS]
    return Option(keyword, "Paper Size" if keyword == "PageSize" else "Page Region", order, "A4", choices)


def paper_lines() -> list[str]:
    lines = ["*DefaultImageableArea: A4"]

    for paper in PAPERS:
        area = [MARGIN, MARGIN, paper.width - MARGIN, paper.length - MARGIN]
        lines.append(f'*ImageableArea {paper.name}/{paper.label}: "{" ".join(points(value) for value in area)}"')

    lines.append("")
    lines.append("*DefaultPaperDimension: A4")

    for paper in PAPERS:
        lines.append(f'*PaperDimension {paper.name}/{paper.label}: "{paper.width} {paper.length}"')

    lines.append("")
    lines.extend(
        [
            "*VariablePaperSize: True",
            f'*MaxMediaWidth: "{CUSTOM_WIDTH[1]}"',
            f'*MaxMediaHeight: "{CUSTOM_LENGTH[1]}"',
            f"*HWMargins: {HARDWARE_MARGIN} {HARDWARE_MARGIN} {HARDWARE_MARGIN} {HARDWARE_MARGIN}",
            '*CustomPageSize True: "pop pop pop <</PageSize [5 -2 roll] /ImagingBBox null>> setpagedevice"',
            f"*ParamCustomPageSize Width: 1 points {CUSTOM_WIDTH[0]} {CUSTOM_WIDTH[1]}",
            f"*ParamCustomPageSize Height: 2 points {CUSTOM_LENGTH[0]} {CUSTOM_LENGTH[1]}",
            "*ParamCustomPageSize WidthOffset: 3 points 0 0",
            "*ParamCustomPageSize HeightOffset: 4 points 0 0",
            "*ParamCustomPageSize Orientation: 5 int 0 1",
            "",
        ]
    )
    return lines


def media_choices() -> list[Choice]:
    choices = [Choice("Default", "Printer Default")]

    for media in MEDIA_TYPES:
        choices.append(Choice(media.name, media.label))
        if media.back_label:
            choices.append(Choice(f"{media.name}Back", media.back_label))

    return choices


def media_names(media: MediaType) -> list[str]:
    return [media.name, f"{media.name}Back"] if media.back_label else [media.name]


def constraint_lines() -> list[str]:
    constraints = Constraints()

    for slot, modules in TRAY_MODULES_FOR_SLOT.items():
        for module in TRAY_MODULES:
            if module.name not in modules:
                constraints.add(("Option1", module.name), ("FXInputSlot", slot))

    for keyword in ("PageSize", "PageRegion"):
        for paper in PAPERS:
            for slot in ALL_TRAYS:
                if slot not in paper.trays:
                    constraints.add((keyword, paper.name), ("FXInputSlot", slot))
            if not paper.duplex:
                for duplex in DUPLEX_CHOICES:
                    constraints.add((keyword, paper.name), ("Duplex", duplex))

    all_media = [name for media in MEDIA_TYPES for name in media_names(media)]
    for slot in ALL_TRAYS:
        if slot != "BypassTray":
            for name in all_media:
                constraints.add(("MediaType", name), ("FXInputSlot", slot))

    for media in MEDIA_TYPES:
        for name in media_names(media):
            duplex_allowed = media.duplex and not name.endswith("Back")
            if not duplex_allowed:
                for duplex in DUPLEX_CHOICES:
                    constraints.add(("MediaType", name), ("Duplex", duplex))

    for paper in PAPERS:
        if paper.media is None:
            continue
        for name in ["Default", *all_media]:
            base = name.removesuffix("Back")
            if base not in paper.media:
                constraints.add(("PageSize", paper.name), ("MediaType", name))

    for preferred in ("Stationary", "FinePaper", "CoatingPaper1"):
        for slot in ALL_TRAYS:
            if slot != "AutoSelect":
                constraints.add(("FXPreferredMediaType", preferred), ("FXInputSlot", slot))

    constraints.lines.append("")
    return constraints.lines


def preset_lines() -> list[str]:
    lines = []

    for name, color_mode, output_mode, ipp_color_mode, quality in PRESETS:
        settings = f"*FXColorMode {color_mode} *FXOutputMode {output_mode}"
        apple_settings = f"com.apple.print.preset.output-mode {ipp_color_mode} com.apple.print.preset.quality {quality}"
        lines.append(f'*APPrinterPreset {name}: "{settings} {apple_settings}"')

    lines.append("")
    return lines


def ppd_lines() -> list[str]:
    installable = [
        Option(
            "InstalledMemory",
            "Installed Memory",
            10,
            "64Meg",
            [Choice(f"{size}Meg", f"{size} MB") for size in (64, 128, 192, 320, 576)],
        ),
        Option("Option1", "Paper Trays", 10, "1Tray-S", TRAY_MODULES),
    ]
    general = [
        paper_option("PageSize", 20),
        paper_option("PageRegion", 20),
        Option(
            "FXInputSlot",
            "Paper Source",
            20,
            "1stTray-S",
            [
                Choice("AutoSelect", "Automatic"),
                Choice("1stTray-S", "Tray 1 (250 Sheets)"),
                Choice("1stTray-H", "Tray 1 (500 Sheets)"),
                Choice("2ndTray-H", "Tray 2 (500 Sheets)"),
                Choice("BypassTray", "Bypass Tray"),
            ],
        ),
        Option("MediaType", "Bypass Tray Paper Type", 20, "Default", media_choices()),
        Option(
            "Duplex",
            "Two-Sided",
            20,
            "None",
            [
                Choice("None", "Off"),
                Choice("DuplexNoTumble", "Long-Edge Binding"),
                Choice("DuplexTumble", "Short-Edge Binding"),
            ],
        ),
        Option(
            "FXColorMode",
            "Color Mode",
            10,
            "Color",
            [Choice("Color", "Color", RASTER_FORMAT), Choice("Black", "Black and White", RASTER_FORMAT)],
        ),
        Option(
            "FXOutputMode",
            "Resolution",
            10,
            "600x600",
            [
                Choice("600x600", "600 dpi", "<</HWResolution[600 600]>>setpagedevice"),
                Choice("300x300", "300 dpi (Faster)", "<</HWResolution[300 300]>>setpagedevice"),
            ],
        ),
        Option("Collate", "Collate Copies", 20, "False", [Choice("False", "Off"), Choice("True", "On")]),
    ]
    advanced = [
        Option(
            "FXDocumentType",
            "Color Correction",
            20,
            "Normal",
            [
                Choice("Normal", "Standard"),
                Choice("Photo", "Photos"),
                Choice("Presentation", "Presentations"),
                Choice("Web", "Web Pages"),
                Choice("POP", "Posters and Signs"),
                Choice("Off", "Off"),
            ],
        ),
        Option(
            "FXPreferredMediaType",
            "Paper Type for Automatic Tray",
            20,
            "Default",
            [
                Choice("Default", "Any"),
                Choice("Stationary", "Plain"),
                Choice("FinePaper", "Bond"),
                Choice("CoatingPaper1", "Coated 1 (60-105 g/m2)"),
            ],
        ),
        Option(
            "FXSubPaperSelection",
            "Substitute Tray",
            20,
            "Policy0",
            [Choice("Policy0", "Printer Default"), Choice("Policy2", "Off, Show a Message"), Choice("SMH", "Bypass Tray")],
        ),
        Option("FXSkip", "Skip Blank Pages", 20, "False", [Choice("False", "Off"), Choice("True", "On")]),
        Option("FXTurnPage", "Rotate 180 Degrees", 20, "False", [Choice("False", "Off"), Choice("True", "On")]),
        Option("FXTonerSaver", "Toner Saver", 20, "False", [Choice("False", "Off"), Choice("True", "On")]),
    ]

    lines = [*HEADER, ""]
    lines.append("*OpenGroup: InstallableOptions/Installable Options")
    for option in installable:
        lines.extend(option_lines(option))
    lines.append("*CloseGroup: InstallableOptions")
    lines.append("")

    lines.extend(constraint_lines())

    lines.append("*OpenGroup: General/General")
    for option in general:
        lines.extend(option_lines(option))
    lines.append("*CloseGroup: General")
    lines.append("")

    lines.extend(paper_lines())
    lines.extend(preset_lines())

    lines.append("*OpenGroup: Advanced/Advanced")
    for option in advanced:
        lines.extend(option_lines(option))
    lines.append("*CloseGroup: Advanced")
    return lines


def main() -> None:
    text = "\n".join(ppd_lines()) + "\n"
    Path(sys.argv[1]).write_text(text, encoding="latin-1", newline="\n")


if __name__ == "__main__":
    main()
