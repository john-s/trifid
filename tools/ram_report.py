#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (C) 2026 john-s
"""Trifid's static RAM report, without the JieLi toolchain.

    tools/ram_report.py [--md FILE]

Compiles the firmware's own sources as the device build does (fm1-base's device/unity.c with Trifid's app.c, and
each o2 unit), with the same configuration flags (app/fm1.toml: USB audio in and out, no Bluetooth), for a 32-bit
little-endian target with this machine's clang (-S, the integrated assembler off: felucca-bsp's inline assembly is
left as text). Every object's section and size then comes out of the assembly (.size), and the sums are checked
against fm1-base's linker map (device/app.ld: RAM 96 KiB for .data + .bss, the pool 0x54000 = 344,064 bytes with
8,192 kept spare: tools/build.py's check) and against Trifid's .pool budget (docs/PRD.md).

It is the same C, so every array's size is exact; what differs from the real link is alignment padding (a few bytes
an object), and the code: RAM code (.ram_text) and flash sizes are for another instruction set, and are not
reported. The device build (./build.sh) prints the real figures ("RAM .data+.bss N B of 98304; pool N B of 344064")
and fails if the pool's spare is under 8 KiB.

The USB audio's cost is measured the same way: the build again without the computer's audio (FM1B_UAC_OUT=0), and
without USB audio at all (FM1B_UAC=0), and the differences.
"""
import argparse
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BASE, APP = ROOT / "base", ROOT / "app"
OUT = APP / "build" / "ram"
RAM_MAX, POOL_MAX, POOL_SPARE = 96 * 1024, 0x54000, 8192
BUDGET = [("fm1-base spare", 8192), ("screen canvas (240x144x2)", 69120), ("strings 2x9,216", 18432),
          ("body delays 2x18,432", 36864), ("Rot", 16384), ("echo 4 s stereo", 176472)]


def compile_s(src, out, defs):
    resdir = subprocess.run(["clang", "-print-resource-dir"], capture_output=True, text=True).stdout.strip()
    inc = [BASE / "bsp" / "chip", BASE / "bsp" / "boards" / "fm1", BASE / "device", BASE / "include", BASE / "lib", APP,
           APP / "build" / "gen", APP / "dsp", src.parent]
    cmd = ["clang", "--target=i386-unknown-none-elf", "-ffreestanding", "-nostdinc", "-isystem", f"{resdir}/include",
           "-S", "-fno-integrated-as", "-Os", "-ffunction-sections", "-fdata-sections", "-fno-builtin", "-fno-common",
           "-w", *[f"-I{d}" for d in inc], *defs, "-o", str(out), str(src)]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        sys.stderr.write(r.stderr)
        raise SystemExit(f"ram_report: {src.name} did not compile")
    return out.read_text()


def objects(asm):
    """[(section, symbol, bytes)] of the data objects in an assembly listing"""
    sect, typ, found = ".text", {}, []
    for ln in asm.splitlines():
        s = ln.strip()
        m = re.match(r"\.section\s+([^,\s]+)", s)
        if m:
            sect = m.group(1)
            continue
        if s in (".text", ".data", ".bss"):
            sect = s
            continue
        m = re.match(r"\.type\s+([\w.$]+),\s*@(\w+)", s)
        if m:
            typ[m.group(1)] = m.group(2)
            continue
        m = re.match(r"\.size\s+([\w.$]+),\s*(\d+)$", s)
        if m and typ.get(m.group(1)) == "object":
            found.append((sect, m.group(1), int(m.group(2))))
            continue
        m = re.match(r"\.(l?comm)\s+([\w.$]+),\s*(\d+)", s)
        if m:
            found.append((".bss", m.group(2), int(m.group(3))))
    return found


def region(sect):
    if sect.startswith(".pool"):
        return "pool"
    if sect.startswith((".data", ".sdata", ".bss", ".sbss")) or sect.endswith(("_data", "_bss")):
        return "ram"
    if sect.startswith(".noinit"):
        return "noinit"
    return "flash"


def measure(defs):
    OUT.mkdir(parents=True, exist_ok=True)
    objs = objects(compile_s(BASE / "device" / "unity.c", OUT / "unity.s", defs))
    for f in sorted((APP / "dsp").glob("*.c")):
        objs += objects(compile_s(f, OUT / (f.stem + ".s"), defs))
    sums = {"pool": 0, "ram": 0, "flash": 0, "noinit": 0}
    for sect, _, n in objs:
        sums[region(sect)] += n
    return objs, sums


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--md", type=Path, help="also write the report as Markdown")
    a = ap.parse_args()
    if not (APP / "build" / "gen" / "felucca_font.h").exists():
        raise SystemExit("ram_report: run ./build.sh --host first (the generated fonts)")
    common = ["-DFELUCCA_UAC_MIDI_LAST=1", '-DFELUCCA_ID="FM-1_7400"', '-DFELUCCA_USB_MANUFACTURER="Trifid"',
              '-DFELUCCA_USB_PRODUCT="TRIFID FM-1"']
    objs, sums = measure(["-DFM1B_UAC=1", "-DFM1B_UAC_OUT=1", *common])
    _, no_out = measure(["-DFM1B_UAC=1", "-DFM1B_UAC_OUT=0", *common])
    _, no_uac = measure(["-DFM1B_UAC=0", "-DFM1B_UAC_OUT=0", *common])
    pool = sorted([o for o in objs if region(o[0]) == "pool"], key=lambda o: -o[2])
    ram = sorted([o for o in objs if region(o[0]) == "ram"], key=lambda o: -o[2])
    L = []
    L.append("# Trifid: static RAM report\n")
    L.append("From `tools/ram_report.py`: the firmware's own sources (fm1-base's `device/unity.c` with Trifid's app, "
             "and the o2 DSP units) compiled with the device build's configuration for a 32-bit target, every "
             "object's size read from the output. Exact sizes; alignment padding and the code are the real link's "
             "(`./build.sh` prints those).\n")
    L.append("## Totals\n")
    L.append("| Region | Used | Limit | Left |\n| --- | ---: | ---: | ---: |")
    L.append(f"| RAM (.data + .bss) | {sums['ram']:,} | {RAM_MAX:,} | {RAM_MAX - sums['ram']:,} |")
    L.append(f"| .pool | {sums['pool']:,} | {POOL_MAX:,} | {POOL_MAX - sums['pool']:,} "
             f"(fm1-base insists on {POOL_SPARE:,} spare) |")
    ok = POOL_MAX - sums["pool"] >= POOL_SPARE and sums["ram"] <= RAM_MAX
    L.append(f"\nVerdict: **{'fits' if ok else 'DOES NOT FIT'}** (pool headroom {POOL_MAX - sums['pool']:,} B, "
             f"RAM headroom {RAM_MAX - sums['ram']:,} B).\n")
    L.append("## .pool against the budget (docs/PRD.md)\n")
    L.append("| Item | Budget | Measured |\n| --- | ---: | ---: |")
    by = {n: s for _, n, s in pool}
    meas = {"screen canvas (240x144x2)": by.get("cv_px", 0), "strings 2x9,216": by.get("str_buf", 0),
            "body delays 2x18,432": by.get("body_buf", 0), "Rot": by.get("rot_buf", 0),
            "echo 4 s stereo": by.get("echo_buf", 0)}
    for name, b in BUDGET:
        L.append(f"| {name} | {b:,} | {meas[name]:,} |" if name in meas else f"| {name} | {b:,} | (kept free) |")
    rest = sums["pool"] - sum(meas.values())
    L.append(f"| everything else in the pool | 0 | {rest:,} |")
    free = POOL_MAX - sums["pool"] - POOL_SPARE
    L.append(f"| free (after the spare) | ≈18,600 | {free:,} |\n")
    L.append("Pool objects:\n")
    L.append("| Object | Bytes |\n| --- | ---: |")
    for _, n, s in pool:
        L.append(f"| `{n}` | {s:,} |")
    L.append("\n## RAM (.data + .bss), the largest\n")
    L.append("| Object | Section | Bytes |\n| --- | --- | ---: |")
    for sect, n, s in ram[:20]:
        L.append(f"| `{n}` | {sect.split('.')[1] if sect.count('.') > 1 else sect} | {s:,} |")
    L.append(f"| ({len(ram) - 20} more) | | {sum(s for _, _, s in ram[20:]):,} |" if len(ram) > 20 else "")
    L.append("\n## USB audio's cost\n")
    L.append("| Build | RAM | Pool |\n| --- | ---: | ---: |")
    L.append(f"| as configured (USB audio in and out) | {sums['ram']:,} | {sums['pool']:,} |")
    L.append(f"| without the computer's audio (`usb_audio_out = false`) | {no_out['ram']:,} | {no_out['pool']:,} |")
    L.append(f"| without USB audio (`usb_audio_in = false`) | {no_uac['ram']:,} | {no_uac['pool']:,} |")
    L.append(f"\nUSB audio costs {sums['ram'] - no_uac['ram']:,} B of RAM and {sums['pool'] - no_uac['pool']:,} B "
             f"of pool in all, {sums['ram'] - no_out['ram']:,} B of RAM of it for the computer's audio (out). Its "
             "rings are in RAM by default (fm1-base `FM1B_UAC_RING_POOL` would move them to the pool).\n")
    text = "\n".join(L) + "\n"
    print(text)
    if a.md:
        a.md.write_text(text)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
