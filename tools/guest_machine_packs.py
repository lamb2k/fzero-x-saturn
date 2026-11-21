"""Stage authored guest player meshes for the native 3D race loader."""
import argparse
import hashlib
import json
from pathlib import Path
import re

from import_source_art import ROOT, output_location
from mach3d_packs import fnv32


def sha(data):
    return hashlib.sha256(data).hexdigest()


def stage(source, output):
    output_location(output)
    output.mkdir(parents=True, exist_ok=True)
    receipt = ["# fzx generation receipt v1",
               f"generator {sha(Path(__file__).read_bytes())} guest_machine_packs.py"]
    header = ["/* Generated from the authored private player kits. */",
              "#pragma once", "static const fzx_mach3d_identity_t guest_machine[2] = {"]
    for family, slug in ((33, "robotnik"), (34, "metal-sonic")):
        folder = source / slug
        pack = (folder / "MACHAB.PAK").read_bytes()
        metadata = (folder / "pack.json").read_bytes()
        declaration = (folder / "closeup_identity.h").read_bytes()
        report = json.loads(metadata)
        split = report["metadata_bytes"]
        if (report["format"] != "FZXMCH02" or sha(pack) != report["pack_sha256"] or
                len(pack) != split + report["texture_bytes"] or not 0 < split < len(pack)):
            raise ValueError(f"{slug}: pack does not match the authored manifest")
        defines = dict(re.findall(r"^#define FZX_CLOSEUP_(\w+) (\d+)u\s*$",
                                  declaration.decode(), re.MULTILINE))
        body = [int(defines["BODY_" + ch]) for ch in "RGB"]
        if int(defines["PACK_HASH"]) != fnv32(pack) or int(defines["META_BYTES"]) != split:
            raise ValueError(f"{slug}: pack identity mismatch")
        fields = (len(pack), fnv32(pack), split, len(pack) - split, fnv32(pack[:split]),
                  report["palette_vram_bytes"], report["gouraud_bytes"])
        header.append("    {" + ", ".join(f"{v}u" for v in fields) +
                      ", {" + ", ".join(map(str, body)) + "}, 1},")
        name = f"MACH3D{family}.PAK"
        (output / name).write_bytes(pack)
        for asset, data in (("MACHAB.PAK", pack), ("pack.json", metadata),
                            ("closeup_identity.h", declaration)):
            receipt.append(f"input {sha(data)} {slug}/{asset}")
        receipt.append(f"output {sha(pack)} {name}")
    header.append("};")
    table = ("\n".join(header) + "\n").encode()
    (output / "guest_machine_identity.h").write_bytes(table)
    receipt.append(f"output {sha(table)} guest_machine_identity.h")
    (output / "guest_machines.receipt").write_bytes(("\n".join(receipt) + "\n").encode())


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="private hybrid-guests/player-3d directory")
    parser.add_argument("--output", type=Path, default=ROOT / "generated/guest_mach3d")
    args = parser.parse_args()
    stage(args.source, args.output)
