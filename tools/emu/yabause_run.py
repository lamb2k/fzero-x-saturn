#!/usr/bin/env python3
"""Boot a Saturn disc image in Yabause headlessly and return a still (card G1.5).

Evidence tier: EMULATOR. Yabause 0.9.15 (Ubuntu 24.04 package `yabause-qt`) with
its emulated (HLE) BIOS: no BIOS file is needed or used. Frame timing from this
emulator is MODELLED at best; a boot here is not a hardware claim.

Each run uses a fresh HOME, so settings never leak between runs (one emulator
instance per lane). The cartridge defaults to the 4 MB RAM cartridge the POC
requires; `--cart none` boots without it.

Pass marker: the image is running and its debug HUD is on screen. The still is
cropped to the emulator viewport and checked for HUD text pixels (yellow) in the
top band and for a non-uniform picture. Stronger markers (AUTOTEST hash) come
from G4.1.

Needs: yabause-qt, xvfb, xdotool, imagemagick.
Usage: tools/emu/yabause_run.py IMAGE.cue --seconds 12 --out still.png [--cart 4mb|1mb|none]
       [--keys "6:Return 8:+z 11:-z"]   # at t seconds press (+), release (-) or tap a key
Exit: 0 pass marker seen, 1 no marker, 2 setup error.
"""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time

# Yabause cartridge ids (cs0.h): CART_DRAM8MBIT = 6 (1 MB), CART_DRAM32MBIT = 7 (4 MB).
CARTS = {"none": 0, "1mb": 6, "4mb": 7}
# Saturn pad button -> (xdotool key name, Qt key code). Use the names with --keys.
PAD = {"up": ("Up", 0x01000013), "right": ("Right", 0x01000014), "down": ("Down", 0x01000015),
       "left": ("Left", 0x01000012), "r": ("e", 0x45), "l": ("q", 0x51),
       "start": ("Return", 0x01000004), "a": ("z", 0x5A), "b": ("x", 0x58), "c": ("c", 0x43),
       "x": ("a", 0x41), "y": ("s", 0x53), "z": ("d", 0x44)}
PAD_KEYS = {name: code for name, (_key, code) in PAD.items()}
TOOLBAR_GUESS = 52  # menu + toolbar height of the Qt window at the default style


def settings(cart: str, realtime: bool = False) -> str:
    return "\n".join((
        "[General]",
        "EnableEmulatedBios=true",
        f"ClockSync={'true' if realtime else 'false'}",
        f"EnableFrameSkipLimiter={'true' if realtime else 'false'}",
        "[Cartridge]",
        f"Type={CARTS[cart]}",
        "[Video]",
        "VideoCore=2",  # software renderer: no GL dependence under Xvfb
        "WinWidth=320",
        "WinHeight=224",
        # Port 1 pad (PERPAD = 2) on the Qt keyboard core (PERCORE_QT = 100). Values are Qt key
        # codes, in PERPAD order: up right down left R L start A B C X Y Z.
        "[Input]",
        "PerCore=100",
        "Port\\1\\Id\\1\\Type=2",
        *(f"Port\\1\\Id\\1\\Controller\\2\\Key\\{n}={code}"
          for n, code in enumerate(PAD_KEYS.values())),
        "[Advanced]",
        "EnableSh2Cache=true",
        "",
    ))


def free_display() -> str:
    for n in range(90, 120):
        if not Path(f"/tmp/.X11-unix/X{n}").exists() and not Path(f"/tmp/.X{n}-lock").exists():
            return f":{n}"
    raise RuntimeError("no free X display number")


def window_id(env: dict, timeout: float = 15.0) -> str:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        found = subprocess.run(("xdotool", "search", "--onlyvisible", "--name", "Yabause"), env=env,
                               capture_output=True, text=True).stdout.split()
        if found:
            return found[-1]
        time.sleep(0.3)
    raise RuntimeError("Yabause window did not appear")


def grab(env: dict, wid: str, out: Path) -> None:
    raw = out.with_suffix(".raw.png")
    # The window can be briefly unviewable (CI saw this): retry, then grab the screen
    # region the window occupies.
    for _attempt in range(3):
        if subprocess.run(("import", "-window", wid, str(raw)), env=env,
                          capture_output=True).returncode == 0:
            break
        time.sleep(0.5)
    else:
        geometry = subprocess.run(("xdotool", "getwindowgeometry", "--shell", wid), env=env,
                                  capture_output=True, text=True, check=True).stdout
        g = dict(line.split("=", 1) for line in geometry.split())
        subprocess.run(("import", "-window", "root", "-crop",
                        f"{g['WIDTH']}x{g['HEIGHT']}+{g['X']}+{g['Y']}", str(raw)),
                       env=env, check=True, capture_output=True)
    # Crop the menu and toolbar, then find the 4:3-ish viewport by trimming the border.
    subprocess.run(("convert", str(raw), "-crop", f"+0+{TOOLBAR_GUESS}", "+repage",
                    "-resize", "320x224!", str(out)), check=True)
    raw.unlink()


def marker(path: Path) -> tuple[bool, str]:
    """HUD yellow pixels in the top band, and more than one colour overall."""
    def count(args: list[str]) -> int:
        result = subprocess.run(("convert", str(path), *args, "-format", "%[fx:mean*w*h]", "info:"),
                                capture_output=True, text=True, check=True)
        return int(float(result.stdout.strip() or 0))
    hud = count(["-crop", "320x40+0+0", "+repage", "-fx", "(r>0.75&&g>0.75&&b<0.35)?1:0"])
    colours = int(subprocess.run(("identify", "-format", "%k", str(path)),
                                 capture_output=True, text=True, check=True).stdout)
    ok = hud >= 200 and colours >= 4
    return ok, f"hud_pixels={hud} colours={colours}"


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("image", type=Path)
    parser.add_argument("--seconds", type=float, default=12.0)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--cart", choices=tuple(CARTS), default="4mb")
    parser.add_argument("--realtime", action="store_true",
                        help="ClockSync on: emulated time follows wall time (checks field rates)")
    parser.add_argument("--keys", default="", help="schedule 't:key', 't:+key', 't:-key'")
    args = parser.parse_args(argv)
    for tool in ("yabause", "Xvfb", "xdotool", "import", "convert"):
        if not shutil.which(tool):
            print(f"emu: missing {tool}", file=sys.stderr)
            return 2
    if not args.image.is_file():
        print(f"emu: no image {args.image}", file=sys.stderr)
        return 2

    with tempfile.TemporaryDirectory(prefix="fzx-emu-") as tmp:
        home = Path(tmp)
        conf = home / ".config/yabause/qt"
        conf.mkdir(parents=True)
        (conf / "yabause.ini").write_text(settings(args.cart, args.realtime), encoding="utf-8")
        runtime = home / "run"
        runtime.mkdir(mode=0o700)
        display = free_display()
        env = dict(os.environ, HOME=str(home), DISPLAY=display, XDG_RUNTIME_DIR=str(runtime))
        xvfb = subprocess.Popen(("Xvfb", display, "-screen", "0", "800x600x24"),
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        emu = None
        try:
            time.sleep(1.0)
            emu = subprocess.Popen(("yabause", "-a", "-nb", "-ns", "-i", str(args.image.resolve())),
                                   env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            wid = window_id(env)
            start = time.monotonic()
            schedule = []
            for item in args.keys.split():
                at, key = item.split(":", 1)
                schedule.append((float(at), key))
            for at, key in sorted(schedule):
                time.sleep(max(0.0, at - (time.monotonic() - start)))
                verb, name = ("keydown", key[1:]) if key[0] == "+" else \
                             ("keyup", key[1:]) if key[0] == "-" else ("key", key)
                # XTEST events to the focused window; Qt ignores XSendEvent input.
                subprocess.run(("xdotool", "windowfocus", "--sync", wid), env=env, check=False,
                               capture_output=True)
                subprocess.run(("xdotool", verb, name), env=env, check=False, capture_output=True)
            time.sleep(max(0.0, args.seconds - (time.monotonic() - start)))
            if emu.poll() is not None:
                print(f"emu: Yabause exited early with {emu.returncode}")
                return 1
            args.out.parent.mkdir(parents=True, exist_ok=True)
            grab(env, wid, args.out)
        except (RuntimeError, subprocess.CalledProcessError) as exc:
            print(f"emu: {exc}", file=sys.stderr)
            return 2
        finally:
            if emu and emu.poll() is None:
                emu.terminate()
                try:
                    emu.wait(5)
                except subprocess.TimeoutExpired:
                    emu.kill()
            xvfb.terminate()
            xvfb.wait(5)
    ok, detail = marker(args.out)
    print(f"emu: {'PASS' if ok else 'FAIL'} cart={args.cart} {detail} still={args.out.name}")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
