"""Read raw dwords out of SimCopter.exe by virtual address (image base 0x400000)."""
import struct
import sys

EXE = r"S:\Repos\sim-copter-remake\Reference\SimCopterOriginalGame\SimCopter.exe"


def load_sections(data):
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    nsec = struct.unpack_from("<H", data, pe + 6)[0]
    opt = struct.unpack_from("<H", data, pe + 20)[0]
    base = struct.unpack_from("<I", data, pe + 24 + 28)[0]
    secs = []
    off = pe + 24 + opt
    for i in range(nsec):
        s = data[off + i * 40: off + (i + 1) * 40]
        name = s[:8].rstrip(b"\0").decode()
        vsize, va, rsize, raw = struct.unpack_from("<IIII", s, 8)
        secs.append((name, base + va, max(vsize, rsize), raw))
    return base, secs


def va_to_off(secs, va):
    for name, start, size, raw in secs:
        if start <= va < start + size:
            return raw + (va - start), name
    return None, None


def main():
    data = open(EXE, "rb").read()
    base, secs = load_sections(data)
    for name, start, size, raw in secs:
        print(f"# section {name:8} VA {start:#010x} size {size:#x} raw {raw:#x}")
    for arg in sys.argv[1:]:
        va, count = arg.split(":") if ":" in arg else (arg, "40")
        va, count = int(va, 16), int(count)
        off, sec = va_to_off(secs, va)
        print(f"\n# dwords at {va:#010x} ({sec}) x{count}")
        for i in range(count):
            v = struct.unpack_from("<I", data, off + i * 4)[0]
            print(f"  +0x{i*4:02x}  {v:#010x}")


if __name__ == "__main__":
    main()
