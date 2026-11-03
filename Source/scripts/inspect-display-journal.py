#!/usr/bin/env python3
"""Decode an explicit research renderer band journal (never timing evidence).

Requires Pillow. Input is a private scratch folder produced by a renderer
compiled with VIM_DISPLAY_JOURNAL, VIM_DISPLAY_JOURNAL_DIR set to that folder,
and an owner-only regular `arm` file created just before the workload. Remove
`arm` to disarm at the next frame; capture otherwise stops at 128 versions or
512 MiB. There is no idle timer/poll, and normal builds omit the journal.
Outputs top/menu and bottom/Dock bands joined together, not a full screenshot.
Existing output paths are refused. No images are written into the repository.
"""
import argparse
import json
from pathlib import Path
import struct

from PIL import Image

HEADER = struct.Struct("<6Q4I")


def decode(path):
    with path.open("rb") as stream:
        prefix = stream.read(HEADER.size)
        if len(prefix) != HEADER.size:
            raise ValueError(f"{path.name}: truncated header")
        magic, version, generation, ticket, epoch, at, width, height, top, bottom = HEADER.unpack(prefix)
        if (magic != 0x314C4E524A4D4956 or version != 1 or
                not 1 <= width <= 8192 or not 1 <= height <= 8192 or
                not 0 < top <= min(64, height) or not 0 <= bottom <= 256 or
                top + bottom > height):
            raise ValueError(f"{path.name}: unsupported geometry/version")
        expected = width * (top + bottom) * 4
        pixels = stream.read(expected + 1)
        if len(pixels) != expected:
            raise ValueError(f"{path.name}: truncated/oversized pixels")
    image = Image.frombytes("RGBA", (width, top + bottom), pixels, "raw", "BGRA")
    return image, dict(File=path.name, Generation=generation, Ticket=ticket, Epoch=epoch, MachTime=at,
                      Width=width, Height=height, TopRows=top, BottomRows=bottom)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("folder", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    paths = sorted(args.folder.glob("frame-*.vimjournal"))
    if not paths or len(paths) > 128:
        parser.error("Expected 1–128 journal files")
    args.output.mkdir(mode=0o700, parents=False, exist_ok=False)
    metadata = []
    for path in paths:
        image, record = decode(path)
        image.save(args.output / (path.stem + ".png"))
        metadata.append(record)
    with (args.output / "Index.json").open("x") as stream:
        json.dump(metadata, stream, indent=2)
    print(f"Decoded {len(metadata)} immutable-source band images; not full frames or FPS evidence")


if __name__ == "__main__":
    main()
