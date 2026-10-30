"""Extracts files from a Skyrim SE (v105) BSA. usage: bsa_extract.py <bsa> <outdir> <substring>..."""
import struct
import sys
from pathlib import Path

import lz4.frame


def entries(path):
    with open(path, "rb") as f:
        head = struct.unpack("<4sIIIIIIII", f.read(36))
        flags, folders, files = head[3], head[4], head[5]
        folder_recs = [struct.unpack("<QIIQ", f.read(24)) for _ in range(folders)]
        recs = []
        for _, n, _, _ in folder_recs:
            name = f.read(f.read(1)[0])[:-1].decode("cp1252")
            for _ in range(n):
                _, size, off = struct.unpack("<QII", f.read(16))
                recs.append((name, size, off))
        names = f.read().split(b"\0")
    return flags, [(f"{d}\\{n.decode('cp1252')}".lower(), s, o) for (d, s, o), n in zip(recs, names)]


def read(path, flags, size, off):
    compressed = bool(flags & 4) != bool(size & 0x40000000)
    size &= 0x3FFFFFFF
    with open(path, "rb") as f:
        f.seek(off)
        if flags & 0x100:
            skip = f.read(1)[0]
            f.read(skip)
            size -= skip + 1
        data = f.read(size)
    return lz4.frame.decompress(data[4:]) if compressed else data


if __name__ == "__main__":
    bsa, out = sys.argv[1], Path(sys.argv[2])
    flags, files = entries(bsa)
    for name, size, off in files:
        if any(s.lower() in name for s in sys.argv[3:]):
            dest = out / name.replace("\\", "/")
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(read(bsa, flags, size, off))
            print(name)
