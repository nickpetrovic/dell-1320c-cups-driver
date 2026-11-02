import json
import shlex
import sys
from pathlib import Path


def main() -> None:
    output = Path(sys.argv[1])
    compiler = sys.argv[2]
    flags = shlex.split(sys.argv[3])
    sources = sys.argv[4:]
    project = Path.cwd()
    entries = [
        {
            "directory": str(project),
            "file": str(project / source),
            "arguments": [compiler, *flags, "-Isrc", "-c", source],
        }
        for source in sources
    ]
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(entries, indent=2) + "\n")


if __name__ == "__main__":
    main()
