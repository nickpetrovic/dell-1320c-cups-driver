import hashlib
import sys
import zlib
from pathlib import Path

import numpy as np

HEADER_SIZE = 48
TABLES = [("Normal", 3), ("Photo", 3), ("Presentation", 3), ("Web", 3), ("POP", 3), ("Black", 1), ("BlackOff", 1)]


def main() -> int:
    recordings = Path(sys.argv[1])
    output = Path(sys.argv[2])
    output.mkdir(parents=True, exist_ok=True)

    for name, channels in TABLES:
        recorded = np.fromfile(recordings / f"cube-{name}.fxr", dtype=np.uint8, offset=HEADER_SIZE)
        table = recorded.reshape(256, 256, 256, channels)
        delta = np.diff(table.astype(np.int16), axis=2, prepend=0).astype(np.uint8)
        packed = zlib.compress(delta.tobytes(), 9)
        table_file = output / f"{name}.lut"
        table_file.write_bytes(packed)

        unpacked = np.frombuffer(zlib.decompress(packed), dtype=np.uint8).reshape(256, 256, 256, channels)
        restored = np.cumsum(unpacked, axis=2, dtype=np.uint8)
        if not np.array_equal(restored, table):
            print(f"{table_file.name}: the compressed table does not decode to the recording", file=sys.stderr)
            return 1
        checksum = hashlib.sha256(table.tobytes()).hexdigest()[:16]
        print(f"{table_file.name}: {len(packed) / 1e6:.2f} MB, sha256 {checksum}, round-trip exact")
    return 0


if __name__ == "__main__":
    sys.exit(main())
