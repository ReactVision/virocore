#!/usr/bin/env python3
"""Relabel a static archive's arm64 iOS device objects as arm64 iOS simulator objects.

For closed-source dependencies with no arm64 simulator slice (GVRAudioSDK). arm64 code is the
same instructions on a device and in the simulator; what tells the linker apart is the object's
platform load command, so rewriting that command is enough for the objects to link and run.

Objects built by older toolchains carry the 16-byte LC_VERSION_MIN_IPHONEOS. Its replacement,
LC_BUILD_VERSION, is 24 bytes, and an object file has no room after its load commands, so every
file offset past them moves. The shift is rounded up to the largest section alignment, keeping
each section's data as aligned in the file as it was before.

Usage: retag_for_simulator.py <thin arm64 input.a> <output.a> [minos, default 15.1]
"""

import os
import struct
import subprocess
import sys
import tempfile

LC_SEGMENT_64 = 0x19
LC_SYMTAB = 0x2
LC_DYSYMTAB = 0xB
LC_VERSION_MIN_IPHONEOS = 0x25
LC_BUILD_VERSION = 0x32
LC_DATA_IN_CODE = 0x29
LC_LINKER_OPTIMIZATION_HINT = 0x2E
LINKEDIT_DATA_COMMANDS = (LC_DATA_IN_CODE, LC_LINKER_OPTIMIZATION_HINT, 0x26, 0x1E, 0x2B, 0x33, 0x34)
PLATFORM_IOS = 2
PLATFORM_IOSSIMULATOR = 7
MH_MAGIC_64 = 0xFEEDFACF
MH_OBJECT = 0x1
S_ZEROFILL_TYPES = (0x1, 0xC, 0x12)


def parse_version(text):
    parts = [int(p) for p in text.split(".")] + [0, 0]
    return (parts[0] << 16) | (parts[1] << 8) | parts[2]


def load_commands(data):
    ncmds, sizeofcmds = struct.unpack_from("<II", data, 16)
    offset = 32
    for _ in range(ncmds):
        cmd, cmdsize = struct.unpack_from("<II", data, offset)
        yield offset, cmd, cmdsize
        offset += cmdsize
    assert offset == 32 + sizeofcmds


def retag_object(data, minos):
    magic, _, _, filetype = struct.unpack_from("<IiiI", data, 0)
    if magic != MH_MAGIC_64 or filetype != MH_OBJECT:
        raise ValueError("not a 64-bit Mach-O object")

    commands = list(load_commands(data))
    by_cmd = {cmd for _, cmd, _ in commands}
    if LC_BUILD_VERSION in by_cmd:
        out = bytearray(data)
        for offset, cmd, _ in commands:
            if cmd == LC_BUILD_VERSION:
                platform, = struct.unpack_from("<I", out, offset + 8)
                if platform not in (PLATFORM_IOS, PLATFORM_IOSSIMULATOR):
                    raise ValueError("unexpected platform %d" % platform)
                struct.pack_into("<II", out, offset + 8, PLATFORM_IOSSIMULATOR,
                                 max(minos, struct.unpack_from("<I", out, offset + 12)[0]))
        return bytes(out)
    if LC_VERSION_MIN_IPHONEOS not in by_cmd:
        raise ValueError("no platform load command")

    ncmds, sizeofcmds = struct.unpack_from("<II", data, 16)
    commands_end = 32 + sizeofcmds
    max_align = 8
    for offset, cmd, _ in commands:
        if cmd == LC_SEGMENT_64:
            nsects, = struct.unpack_from("<I", data, offset + 64)
            for i in range(nsects):
                align, = struct.unpack_from("<I", data, offset + 72 + i * 80 + 52)
                max_align = max(max_align, 1 << align)
    grow = 24 - 16
    shift = -(-grow // max_align) * max_align

    header = bytearray(data[:32])
    struct.pack_into("<I", header, 20, sizeofcmds + grow)
    new_commands = bytearray()

    def moved(value):
        return value + shift if value else 0

    for offset, cmd, cmdsize in commands:
        body = bytearray(data[offset:offset + cmdsize])
        if cmd == LC_VERSION_MIN_IPHONEOS:
            version, sdk = struct.unpack_from("<II", body, 8)
            body = bytearray(struct.pack("<IIIIII", LC_BUILD_VERSION, 24, PLATFORM_IOSSIMULATOR,
                                         max(minos, version), sdk, 0))
        elif cmd == LC_SEGMENT_64:
            fileoff, filesize = struct.unpack_from("<QQ", body, 40)
            if filesize:
                struct.pack_into("<Q", body, 40, fileoff + shift)
            nsects, = struct.unpack_from("<I", body, 64)
            for i in range(nsects):
                s = 72 + i * 80
                sect_offset, _, reloff, nreloc, flags = struct.unpack_from("<IIIII", body, s + 48)
                if sect_offset and (flags & 0xFF) not in S_ZEROFILL_TYPES:
                    struct.pack_into("<I", body, s + 48, sect_offset + shift)
                if nreloc:
                    struct.pack_into("<I", body, s + 56, reloff + shift)
        elif cmd == LC_SYMTAB:
            symoff, nsyms, stroff, strsize = struct.unpack_from("<IIII", body, 8)
            struct.pack_into("<IIII", body, 8, moved(symoff) if nsyms else symoff, nsyms,
                             moved(stroff) if strsize else stroff, strsize)
        elif cmd == LC_DYSYMTAB:
            # (offset field, count field) pairs, as byte offsets into dysymtab_command.
            for off_at, count_at in ((32, 36), (40, 44), (48, 52), (56, 60), (64, 68), (72, 76)):
                value, count = struct.unpack_from("<II", body, off_at)
                if count:
                    struct.pack_into("<I", body, off_at, value + shift)
        elif cmd in LINKEDIT_DATA_COMMANDS:
            dataoff, datasize = struct.unpack_from("<II", body, 8)
            if datasize:
                struct.pack_into("<I", body, 8, dataoff + shift)
        elif cmd not in (0x2D,):  # LC_LINKER_OPTION carries no offsets
            raise ValueError("unhandled load command 0x%x" % cmd)
        new_commands += body

    padding = bytes(shift - grow)
    return bytes(header) + bytes(new_commands) + padding + data[commands_end:]


def archive_members(data):
    if not data.startswith(b"!<arch>\n"):
        raise ValueError("not an ar archive (thin it with lipo first)")
    offset = 8
    while offset < len(data):
        header = data[offset:offset + 60]
        name = header[:16].decode().rstrip()
        size = int(header[48:58].decode())
        body = data[offset + 60:offset + 60 + size]
        if name.startswith("#1/"):
            name_len = int(name[3:])
            name = body[:name_len].rstrip(b"\0").decode()
            body = body[name_len:]
        yield name, body
        offset += 60 + size + (size & 1)


def main():
    source, output = sys.argv[1], sys.argv[2]
    minos = parse_version(sys.argv[3] if len(sys.argv) > 3 else "15.1")
    with open(source, "rb") as f:
        data = f.read()

    with tempfile.TemporaryDirectory() as work:
        objects = []
        for index, (name, body) in enumerate(archive_members(data)):
            if name.startswith("__.SYMDEF"):
                continue
            # One directory per member: archives may hold two members with the same name.
            member_dir = os.path.join(work, str(index))
            os.mkdir(member_dir)
            path = os.path.join(member_dir, name)
            with open(path, "wb") as f:
                f.write(retag_object(body, minos))
            objects.append(path)
        if os.path.exists(output):
            os.remove(output)
        subprocess.run(["xcrun", "libtool", "-static", "-no_warning_for_no_symbols",
                        "-o", output] + objects, check=True)
    print("%s: %d objects retagged for the arm64 simulator" % (output, len(objects)))


if __name__ == "__main__":
    main()
