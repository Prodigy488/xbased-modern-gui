#!/usr/bin/env python3
"""ips2mod.py DISC.iso PATCH.ips [XBE] [--name N] [--desc D] [--author A]:
an IPS patch of an XBE (file offsets, as Lunar IPS / FLIPS apply them to
default.xbe) as a mod.txt (mods/README.md): each record's bytes at the
virtual address its offset loads at, with the disc's bytes there as the
`old ->` check, so a patch made for another release of the game is skipped
rather than applied blindly. The report (stderr) shows each record in
context (capstone, when installed) to judge whether it fits this disc.

XBE: the executable's path on the disc (default.xbe).
"""
import argparse
import struct
import sys


def xdvdfs_file(iso, path):
    """The bytes of path (\\ or / separated) in an XDVDFS image."""
    f = open(iso, 'rb')

    def rd(off, n):
        f.seek(off)
        return f.read(n)

    base = None
    for b in (0, 0x18300000, 0xFD90000, 0x30600000):
        if rd(b + 32 * 2048, 20) == b'MICROSOFT*XBOX*MEDIA':
            base = b
            break
    if base is None:
        sys.exit('not an Xbox disc image: ' + iso)
    sec, size = struct.unpack('<II', rd(base + 32 * 2048 + 20, 8))
    for part in path.replace('/', '\\').split('\\'):
        data = rd(base + sec * 2048, size)

        def walk(o):
            if o * 4 + 14 > len(data):
                return None
            l, r, s, sz, attr, nl = struct.unpack('<HHIIBB', data[o * 4:o * 4 + 14])
            if l == 0xFFFF:
                return None
            nm = data[o * 4 + 14:o * 4 + 14 + nl].decode('latin1')
            if nm.lower() == part.lower():
                return s, sz
            return (walk(l) if l else None) or (walk(r) if r else None)
        hit = walk(0)
        if not hit:
            sys.exit('%s: no %s on the disc' % (iso, path))
        sec, size = hit
    return rd(base + sec * 2048, size)


def ips_records(blob):
    if blob[:5] != b'PATCH':
        sys.exit('not an IPS patch')
    i, out = 5, []
    while blob[i:i + 3] != b'EOF':
        off = int.from_bytes(blob[i:i + 3], 'big')
        n = int.from_bytes(blob[i + 3:i + 5], 'big')
        i += 5
        if n:
            out.append((off, blob[i:i + n]))
            i += n
        else:                       # RLE
            rn = int.from_bytes(blob[i:i + 2], 'big')
            out.append((off, blob[i + 2:i + 3] * rn))
            i += 3
    return out


def va_of(xbe, off):
    base, hdr = struct.unpack_from('<II', xbe, 0x104)
    if off < hdr:
        return base + off
    nsec, secva = struct.unpack_from('<II', xbe, 0x11C)
    for k in range(nsec):
        fl, va, vs, raw, rs = struct.unpack_from('<IIIII', xbe, secva - base + k * 0x38)
        if raw <= off < raw + rs:
            return va + (off - raw)
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('iso')
    ap.add_argument('ips')
    ap.add_argument('xbe', nargs='?', default='default.xbe')
    ap.add_argument('--name', default='60 fps')
    ap.add_argument('--desc', default='')
    ap.add_argument('--author', default='')
    ap.add_argument('--refresh', type=int, default=0)
    a = ap.parse_args()
    xbe = xdvdfs_file(a.iso, a.xbe)
    base = struct.unpack_from('<I', xbe, 0x104)[0]
    cert = struct.unpack_from('<I', xbe, 0x118)[0] - base
    tid, = struct.unpack_from('<I', xbe, cert + 8)
    ver, = struct.unpack_from('<I', xbe, cert + 0xAC)
    try:
        from capstone import Cs, CS_ARCH_X86, CS_MODE_32
        md = Cs(CS_ARCH_X86, CS_MODE_32)
    except ImportError:
        md = None
    print('%s: title %08X version %d (mods/%08X.%d/)' % (a.xbe, tid, ver, tid, ver), file=sys.stderr)
    lines, same = [], 0
    for off, new in ips_records(open(a.ips, 'rb').read()):
        va = va_of(xbe, off)
        old = xbe[off:off + len(new)]
        if va is None or len(old) != len(new):
            print('  %06x: past the image\'s sections, skipped' % off, file=sys.stderr)
            continue
        same += old == new
        print('  %06x -> va %08x: %s -> %s' % (off, va, old.hex(' '), new.hex(' ')), file=sys.stderr)
        if md:
            lo = max(0, off - 12)
            for ins in md.disasm(xbe[lo:off + len(new) + 12], va - (off - lo)):
                mark = '>' if va <= ins.address < va + len(new) else ' '
                print('     %s %08x  %-22s %s %s' % (mark, ins.address, ins.bytes.hex(), ins.mnemonic, ins.op_str),
                      file=sys.stderr)
        lines.append('0x%08X: %s -> %s' % (va, old.hex(' ').upper(), new.hex(' ').upper()))
    if same:
        print('  %d records already hold their bytes (an XBE patched before?)' % same, file=sys.stderr)
    print('name: %s' % a.name)
    if a.desc:
        print('description: %s' % a.desc)
    if a.author:
        print('author: %s' % a.author)
    if a.refresh:
        print('refresh: %d' % a.refresh)
    print('\n# from %s (ips2mod.py)\n[%s]' % (a.ips.replace('\\', '/').split('/')[-1], a.xbe))
    print('\n'.join(lines))


if __name__ == '__main__':
    main()
