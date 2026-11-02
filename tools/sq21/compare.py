import argparse
import random
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

VENDOR_COMPRESSOR = Path("/Library/Printers/FujiXerox/Filter/fxmhbpl.bundle/Contents/MacOS/FXM_ALC")
VENDOR_PPD = Path("/Library/Printers/PPDs/Contents/Resources/FX DocuPrint C525 A-AP.gz")
VENDOR_HEADER_SIZE = 48


def vendor_compress(ppd: Path, pixels: bytes, width: int, height: int, dpi: int, gray: bool) -> bytes:
    channels = 1 if gray else 3
    header = struct.pack("<12I", width, height, channels * 8, width * channels, len(pixels), dpi, dpi, 0, 0, 0 if gray else 1, 0, 1)
    options = "PageSize=Letter" + (" FXColorMode=Black" if gray else "")
    command = [VENDOR_COMPRESSOR, "1", "user", "title", "1", options]
    result = subprocess.run(command, input=header + pixels, capture_output=True, env={"PPD": str(ppd)}, check=True)
    return result.stdout[VENDOR_HEADER_SIZE:]


def our_compress(tool: Path, pixels: bytes, width: int, dpi: int, gray: bool) -> bytes:
    mode = "gray" if gray else "rgb"
    return subprocess.run([tool, mode, str(width), str(dpi)], input=pixels, capture_output=True, check=True).stdout


def smooth_image(generator: random.Random, width: int, height: int, channels: int) -> bytes:
    base = [generator.randint(0, 255) for _ in range(channels)]
    spread = generator.choice([2, 6, 12, 25, 60])
    pixels = bytearray()
    for _ in range(width * height):
        if generator.random() < 0.3:
            base = [min(255, max(0, value + generator.randint(-spread, spread))) for value in base]
        pixels += bytes(min(255, max(0, value + generator.randint(-spread // 2, spread // 2))) for value in base)
    return bytes(pixels)


def few_color_image(generator: random.Random, width: int, height: int, channels: int) -> bytes:
    palette = generator.sample([*range(32), 128, 254, 255], generator.randint(1, 4))
    pixels = bytearray()
    for _ in range(height):
        value = generator.choice(palette)
        for _ in range(width):
            if generator.random() < 0.25:
                value = generator.choice(palette)
            pixels += bytes([value] * channels)
    return bytes(pixels)


def main() -> int:
    parser = argparse.ArgumentParser(description="Compare build/sq21-encode with the vendor's SQ21 compressor on random images.")
    parser.add_argument("tool", type=Path, help="path to sq21-encode")
    parser.add_argument("--trials", type=int, default=500)
    parser.add_argument("--seed", type=int, default=1)
    arguments = parser.parse_args()

    if not VENDOR_COMPRESSOR.exists():
        print(f"the Fuji Xerox driver is not installed ({VENDOR_COMPRESSOR} is missing)", file=sys.stderr)
        return 2

    work = Path(tempfile.mkdtemp(prefix="dell1320c-sq21-compare."))
    ppd = work / "vendor.ppd"
    ppd.write_bytes(subprocess.run(["gzcat", VENDOR_PPD], capture_output=True, check=True).stdout)

    generator = random.Random(arguments.seed)
    mismatches = 0
    for trial in range(arguments.trials):
        gray = generator.random() < 0.5
        channels = 1 if gray else 3
        width = generator.choice([8, 16, generator.randint(1, 300)])
        height = generator.randint(1, 6)
        dpi = generator.choice([300, 600])
        make_image = generator.choice([smooth_image, few_color_image])
        pixels = make_image(generator, width, height, channels)
        if vendor_compress(ppd, pixels, width, height, dpi, gray) != our_compress(arguments.tool, pixels, width, dpi, gray):
            mismatches += 1
            failed = work / f"mismatch-{trial}-{'gray' if gray else 'rgb'}-{width}x{height}-{dpi}.raw"
            failed.write_bytes(pixels)
            print(f"mismatch: {failed}")

    print(f"{mismatches} mismatches in {arguments.trials} images")
    if not mismatches:
        shutil.rmtree(work)
    return 1 if mismatches else 0


if __name__ == "__main__":
    sys.exit(main())
