import struct
import sys
from pathlib import Path

import numpy as np

SIDE = 4096
RESOLUTION = 600


def main() -> None:
    stride = SIDE * 3
    color_index = np.arange(SIDE * SIDE, dtype=np.uint32)
    pixels = np.empty((SIDE * SIDE, 3), dtype=np.uint8)
    pixels[:, 0] = (color_index >> 16) & 255
    pixels[:, 1] = (color_index >> 8) & 255
    pixels[:, 2] = color_index & 255
    header = struct.pack("<12I", SIDE, SIDE, 24, stride, stride * SIDE, RESOLUTION, RESOLUTION, 0, 0, 1, 0, 1)
    Path(sys.argv[1]).write_bytes(header + pixels.tobytes())


if __name__ == "__main__":
    main()
