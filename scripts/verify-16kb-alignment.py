#!/usr/bin/env python3
"""
verify-16kb-alignment.py — fail if any arm64-v8a .so in an AAR/APK/AAB is not
16KB page-aligned (ReactVision/viro#485, Android 15 / SDK 35 requirement).

Android 15 mandates that 64-bit native libraries load on devices using 16KB
memory pages. The ELF requirement: every PT_LOAD segment's p_align must be
>= 0x4000 (16384). The legacy default was 0x1000 (4KB).

Usage:
    python3 verify-16kb-alignment.py <file.aar|file.apk|file.aab> [--abi arm64-v8a]

Exit codes:
    0  all checked libraries are >= 16KB aligned
    1  at least one library is < 16KB aligned (the failure)
    2  usage / file error

No third-party deps — parses ELF program headers directly so it runs in CI
without pyelftools.
"""
import sys
import zipfile
import struct
import argparse


def min_pt_load_align(elf_bytes: bytes) -> int:
    """Return the MINIMUM PT_LOAD p_align in a 64-bit little-endian ELF.

    Every PT_LOAD segment must meet the 16KB bar — a mixed-alignment ELF
    (e.g. one 0x4000 and one 0x1000 segment) still fails to load on 16KB-page
    devices. Returning the min lets the caller gate on the worst offender.
    Returns -1 for 32-bit ELFs (not subject to the requirement).
    """
    if elf_bytes[:4] != b"\x7fELF":
        raise ValueError("not an ELF file")
    ei_class = elf_bytes[4]  # 1=32-bit, 2=64-bit
    if ei_class != 2:
        return -1
    e_phoff = struct.unpack_from("<Q", elf_bytes, 0x20)[0]
    e_phentsize = struct.unpack_from("<H", elf_bytes, 0x36)[0]
    e_phnum = struct.unpack_from("<H", elf_bytes, 0x38)[0]
    PT_LOAD = 1
    min_align = None
    for i in range(e_phnum):
        off = e_phoff + i * e_phentsize
        p_type = struct.unpack_from("<I", elf_bytes, off)[0]
        if p_type == PT_LOAD:
            p_align = struct.unpack_from("<Q", elf_bytes, off + 0x30)[0]
            min_align = p_align if min_align is None else min(min_align, p_align)
    if min_align is None:
        raise ValueError("no PT_LOAD segments")
    return min_align


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("archive", help="AAR / APK / AAB path")
    ap.add_argument("--abi", default="arm64-v8a",
                    help="ABI to check (default arm64-v8a; only 64-bit matters)")
    ap.add_argument("--min-align", type=lambda x: int(x, 0), default=0x4000,
                    help="minimum required p_align (default 0x4000 = 16KB)")
    args = ap.parse_args()

    try:
        zf = zipfile.ZipFile(args.archive)
    except Exception as e:
        print(f"ERROR: cannot open {args.archive}: {e}")
        return 2

    # .so entries can be nested (an AAR bundles other AARs' libs after merge,
    # and prefab modules live under prefab/...). Match any path ending in the
    # ABI dir + a .so, plus prefab module libs for the target ABI.
    so_entries = [
        n for n in zf.namelist()
        if n.endswith(".so") and (f"/{args.abi}/" in n or f".{args.abi}/" in n)
    ]
    if not so_entries:
        print(f"WARNING: no {args.abi} .so files found in {args.archive}")
        return 0

    failures = []
    ok = []
    for name in sorted(so_entries):
        data = zf.read(name)
        try:
            align = min_pt_load_align(data)
        except ValueError as e:
            print(f"  skip (not ELF): {name} ({e})")
            continue
        if align == -1:
            continue  # 32-bit, not applicable
        if align >= args.min_align:
            ok.append((name, align))
        else:
            failures.append((name, align))

    for name, align in ok:
        print(f"  OK   {hex(align):>8}  {name}")
    for name, align in failures:
        print(f"  FAIL {hex(align):>8}  {name}  (< {hex(args.min_align)})")

    if failures:
        print(f"\n{len(failures)} library/libraries are not {hex(args.min_align)}-aligned.")
        return 1
    print(f"\nAll {len(ok)} arm64-v8a libraries are >= {hex(args.min_align)}-aligned.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
