"""Stage authored guest player meshes for the native 3D race loader."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

from import_source_art import ROOT, output_location
from mach3d_packs import fnv32


APPROVED = {
    33: ("robotnik", "d6b91616f976852af222d01d2924f511afd32a13acf7581dced3f719f822e68f",
         "c7a7e3e2281fc93cfa7f375c4d18187feaa0664fdbfe18bb23214c5166e0cc52"),
    34: ("metal-sonic", "71adbc486e6a69645cbd815cf7021f3a7562df35bf63d4277a8d1d570a0b28f5",
         "c0229892f6ccca910cf4a48c891609979a979034ade0b8f822ab963ca003c737"),
}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def stage(source, output, fx_output):
    output_location(fx_output)
    output_location(output)
    output.mkdir(parents=True, exist_ok=True)
    receipt = ["# fzx generation receipt v1",
               f"generator {sha(Path(__file__).read_bytes())} guest_machine_packs.py"]
    header = ["/* Generated from the authored private player kits. */",
              "#pragma once", "static const fzx_mach3d_identity_t guest_machine[2] = {"]
    files = {}
    rows = {}
    for family, (slug, pack_hash, socket_hash) in APPROVED.items():
        folder = source / slug
        pack = (folder / "MACHAB.PAK").read_bytes()
        metadata = (folder / "pack.json").read_bytes()
        declaration = (folder / "closeup_identity.h").read_bytes()
        sockets = (folder / "BOOSTERS.BIN").read_bytes()
        if sha(pack) != pack_hash or sha(sockets) != socket_hash:
            raise ValueError(f"{slug}: approved model or booster row mismatch")
        rows[str(family)] = struct.unpack(">17h", sockets)
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
        files[name] = pack
        for asset, data in (("MACHAB.PAK", pack), ("pack.json", metadata),
                            ("closeup_identity.h", declaration), ("BOOSTERS.BIN", sockets)):
            receipt.append(f"input {sha(data)} {slug}/{asset}")
    header.append("};")
    table = ("\n".join(header) + "\n").encode()
    files["guest_machine_identity.h"] = table
    files["guest_boosters.json"] = (json.dumps(rows, indent=2) + "\n").encode()
    for name, data in files.items():
        (output / name).write_bytes(data)
        receipt.append(f"output {sha(data)} {name}")
    fx_output.mkdir(parents=True, exist_ok=True)
    (fx_output / "guest_boosters.json").write_bytes(files["guest_boosters.json"])
    (output / "guest_machines.receipt").write_bytes(("\n".join(receipt) + "\n").encode())


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="private hybrid-guests/blender-shipping/packs directory")
    parser.add_argument("--output", type=Path, default=ROOT / "generated/guest_mach3d")
    parser.add_argument("--fx-output", type=Path, default=ROOT / "generated/fx")
    args = parser.parse_args()
    stage(args.source, args.output, args.fx_output)
