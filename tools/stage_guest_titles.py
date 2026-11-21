#!/usr/bin/env python3
"""Stage the approved title packs from private review evidence."""
import argparse
import hashlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FILES = ("TITLE.PAK", "title_identity.h", "title.json")
# Fingerprint the ordered binary SHA-256 digests of FILES, including the layout.
PACKS = (
    ("codex-c123-brand/native-title", "title_guest",
     "762cf89112aee81a906756d00ff1edeb165a6adce49fc5154ce8b55c7222314b"),
    ("codex-c124-brand/Falcon", "title_guest_falcon",
     "124fbfca4d0639bfda25ab7795617eb4c68507eaf44dc71cd7dbd87e311c53ff"),
    ("codex-c126-brand/Comic", "title_guest_comic",
     "26b16eaa07a7afea4819021f1d09ee9e85eeb58ff4968cab69b9f65a112515f7"),
)


def stage(evidence):
    packs = []
    for source, destination, expected in PACKS:
        files = {name: (evidence / "host" / source / name).read_bytes() for name in FILES}
        hashes = [hashlib.sha256(data).digest() for data in files.values()]
        if hashlib.sha256(b"".join(hashes)).hexdigest() != expected:
            raise SystemExit(f"{source}: private title pack differs from the reviewed pack")
        packs.append((destination, files))

    generator = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    for destination, files in packs:
        out = ROOT / "generated" / destination
        out.mkdir(parents=True, exist_ok=True)
        receipt = ["# fzx generation receipt v1; regenerate, never edit",
                   f"generator {generator} stage_guest_titles.py"]
        for name, data in files.items():
            (out / name).write_bytes(data)
            receipt.append(f"output {hashlib.sha256(data).hexdigest()} {name}")
        (out / "title.receipt").write_bytes(("\n".join(receipt) + "\n").encode())
        print(f"Staged generated/{destination}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("evidence", type=Path, help="private FZX evidence root")
    stage(parser.parse_args().evidence)
