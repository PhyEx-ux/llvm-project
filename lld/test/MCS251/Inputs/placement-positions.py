#!/usr/bin/env python3
"""G11-D2 (design section 11.1): the INDEPENDENT positioning-carrier byte
checker.  It re-implements the frozen section-3 layout from the design text
and never calls the producer's serializer; expected values come from the
test's frozen literals, not from the tool under test.

Subcommands:
  check <elf> --object <file>... [--expect <spec>...]
      Validate the whole frozen schema and print one canonical line per
      record for FileCheck.  --object lists the link inputs IN ORDER (their
      SHA-256 digests are computed here and must match the carrier's table).
      --expect specs (repeatable):
        rec <object_id> <input_shndx> <space> <input_size>
            [slice <off> <addr> <len>]...
        section-offset-aligned / section-offset <val>
        section-size <val>
      Exit 0 only when everything matches.

  dump <elf>
      Print the canonical dump only.

  same-carrier <elf1> <elf2>
      Byte-compare the carrier sections of two final ELFs (determinism and
      report-only vs verify-only equality).
"""
import hashlib
import struct
import sys

SHT_NOTE = 7


def die(msg):
    sys.stderr.write('placement-positions: ' + msg + '\n')
    sys.exit(1)


def u32(b, o):
    return struct.unpack_from('>I', b, o)[0]


def p32(v):
    return struct.pack('>I', v)


def sections(b):
    shoff = u32(b, 32)
    shent = struct.unpack_from('>H', b, 46)[0]
    shnum = struct.unpack_from('>H', b, 48)[0]
    shstrn = struct.unpack_from('>H', b, 50)[0]
    out = []
    for i in range(shnum):
        o = shoff + i * shent
        out.append(dict(i=i, nameoff=u32(b, o), type=u32(b, o + 4),
                        flags=u32(b, o + 8), addr=u32(b, o + 12),
                        off=u32(b, o + 16), size=u32(b, o + 20),
                        link=u32(b, o + 24), info=u32(b, o + 28),
                        align=u32(b, o + 32), entsize=u32(b, o + 36),
                        hdroff=o))
    so = out[shstrn]

    def nm(x):
        s = b[so['off'] + x['nameoff']:so['off'] + so['size']]
        return s.split(b'\0')[0].decode()
    for s in out:
        s['name'] = nm(s)
    return out


def phdrs(b):
    phoff = u32(b, 28)
    phnum = struct.unpack_from('>H', b, 44)[0]
    return [dict(off=u32(b, phoff + i * 32 + 4),
                 vaddr=u32(b, phoff + i * 32 + 8),
                 filesz=u32(b, phoff + i * 32 + 16),
                 memsz=u32(b, phoff + i * 32 + 20))
            for i in range(phnum)]


def parse(elf):
    b = open(elf, 'rb').read()
    hits = [s for s in sections(b)
            if s['name'] == '.mcs251.placement.positions']
    if len(hits) != 1:
        die('expected exactly one carrier section, found %d' % len(hits))
    c = hits[0]
    # frozen section shape (design 3.1)
    if (c['type'] != SHT_NOTE or c['flags'] != 0 or c['addr'] != 0 or
            c['link'] != 0 or c['info'] != 0 or c['align'] != 4 or
            c['entsize'] != 0):
        die('carrier header is not the frozen SHT_NOTE shape')
    if c['off'] % 4 != 0:
        die('carrier is not at a 4-byte file offset')
    if c['off'] + c['size'] > len(b):
        die('carrier is out of file bounds')
    for s in sections(b):
        if s['i'] == c['i'] or s['type'] == 8 or s['size'] == 0:
            continue
        if c['off'] < s['off'] + s['size'] and s['off'] < c['off'] + c['size']:
            die('carrier reuses the file bytes of ' + s['name'])
    body = b[c['off']:c['off'] + c['size']]
    # envelope (design 3.2)
    if u32(body, 0) != 7:
        die('namesz != 7')
    descsz = u32(body, 4)
    if u32(body, 8) != 3:
        die('note type != 3')
    if body[12:20] != b'MCS251\x00\x00':
        die('note name is not MCS251\\0 + zero pad')
    if descsz != len(body) - 20:
        die('descsz does not cover the section exactly')
    # descriptor header (design 3.3)
    version, objn, recn, reserved = struct.unpack_from('>IIII', body, 20)
    if version != 1:
        die('position_version != 1')
    if reserved != 0:
        die('descriptor reserved != 0')
    digests = []
    for i in range(objn):
        digests.append(body[36 + 32 * i:36 + 32 * (i + 1)])
    # records (design 3.5/3.6)
    recs = []
    off = 36 + 32 * objn
    prev = None
    for ri in range(recn):
        if off + 24 > len(body):
            die('record %d truncated' % ri)
        rsz, oid, shndx = struct.unpack_from('>III', body, off)
        space = body[off + 12]
        if body[off + 13] != 0 or struct.unpack_from('>H', body, off + 14)[0]:
            die('record %d reserved fields nonzero' % ri)
        isize, scount = struct.unpack_from('>II', body, off + 16)
        if rsz != 24 + 12 * scount:
            die('record %d size mismatch' % ri)
        if oid >= objn:
            die('record %d object_id out of range' % ri)
        if shndx == 0:
            die('record %d input_shndx zero' % ri)
        key = (oid << 32) | shndx
        if prev is not None and key <= prev:
            die('record %d breaks the strict (object,shndx) order' % ri)
        prev = key
        slices = []
        so = off + 24
        total = 0
        for k in range(scount):
            io, fa, ln = struct.unpack_from('>III', body, so)
            slices.append((io, fa, ln))
            if k == 0 and io != 0:
                die('record %d first slice offset != 0' % ri)
            if k:
                pio, pfa, pln = slices[k - 1]
                if io != pio + pln:
                    die('record %d slice offsets not contiguous' % ri)
                if fa != pfa + pln:
                    die('record %d slice addresses not contiguous' % ri)
            total += ln
            so += 12
        if total != isize:
            die('record %d slice lengths do not sum to input_size' % ri)
        if isize == 0 and (scount != 1 or slices[0][2] != 0):
            die('record %d zero-length shape wrong' % ri)
        if isize != 0 and scount == 1 and slices[0][2] == 0:
            die('record %d non-empty with zero slice' % ri)
        recs.append(dict(object=oid, shndx=shndx, space=space,
                         size=isize, slices=slices))
        off += rsz
    if off != len(body):
        die('unconsumed bytes after the record table')
    return dict(buf=b, sec=c, body=body, objn=objn, digests=digests,
                recs=recs, phdrs=phdrs(b))


def dump(parsed):
    print('pos: objects=%d records=%d' % (parsed['objn'],
                                          len(parsed['recs'])))
    for i, dg in enumerate(parsed['digests']):
        print('pos: digest[%d]=%s' % (i, dg.hex()))
    for r in parsed['recs']:
        sl = ' '.join('%d,%#x,%#x' % s for s in r['slices'])
        print('pos: rec object=%d shndx=%d space=%d size=%#x slices=%s'
              % (r['object'], r['shndx'], r['space'], r['size'], sl))


def check(elf, objects, expects):
    p = parse(elf)
    if len(objects) != p['objn']:
        die('object count: carrier %d, given %d' % (p['objn'],
                                                    len(objects)))
    for i, f in enumerate(objects):
        h = hashlib.sha256(open(f, 'rb').read()).digest()
        if h != p['digests'][i]:
            die('object %d fingerprint mismatch (%s)' % (i, f))
    # expectations
    i = 0
    while i < len(expects):
        e = expects[i]
        if e == 'rec':
            oid, shndx, space, size = (int(expects[i + 1], 0),
                                       int(expects[i + 2], 0),
                                       int(expects[i + 3], 0),
                                       int(expects[i + 4], 0))
            i += 5
            sl = []
            while i < len(expects) and expects[i] == 'slice':
                sl.append((int(expects[i + 1], 0), int(expects[i + 2], 0),
                           int(expects[i + 3], 0)))
                i += 4
            hit = [r for r in p['recs'] if r['object'] == oid
                   and r['shndx'] == shndx]
            if len(hit) != 1:
                die('expected record (%d,%d): found %d' % (oid, shndx,
                                                           len(hit)))
            r = hit[0]
            if r['space'] != space or r['size'] != size or r['slices'] != sl:
                die('record (%d,%d) = space %d size %#x slices %r, '
                    'expected space %d size %#x slices %r'
                    % (oid, shndx, r['space'], r['size'], r['slices'],
                       space, size, sl))
        elif e == 'section-offset':
            if p['sec']['off'] != int(expects[i + 1], 0):
                die('section offset %#x != expected %#x'
                    % (p['sec']['off'], int(expects[i + 1], 0)))
            i += 2
        elif e == 'section-offset-aligned':
            pass
        elif e == 'section-size':
            if p['sec']['size'] != int(expects[i + 1], 0):
                die('section size mismatch')
            i += 2
        elif e == 'no-record':
            oid, shndx = int(expects[i + 1], 0), int(expects[i + 2], 0)
            if any(r['object'] == oid and r['shndx'] == shndx
                   for r in p['recs']):
                die('record (%d,%d) exists but must be absent' % (oid, shndx))
            i += 3
        else:
            die('unknown expectation ' + e)
    dump(p)


def same_carrier(a, b):
    pa = parse(a)
    pb = parse(b)
    if pa['body'] != pb['body']:
        die('carrier bytes differ between %s and %s' % (a, b))
    print('same-carrier: %d bytes identical' % len(pa['body']))


def main():
    if len(sys.argv) < 3:
        die(__doc__)
    cmd = sys.argv[1]
    if cmd == 'dump':
        dump(parse(sys.argv[2]))
    elif cmd == 'same-carrier':
        same_carrier(sys.argv[2], sys.argv[3])
    elif cmd == 'check':
        elf = sys.argv[2]
        objects = []
        expects = []
        i = 3
        mode = None
        while i < len(sys.argv):
            if sys.argv[i] == '--object':
                mode = 'object'
                i += 1
                continue
            if sys.argv[i] == '--expect':
                mode = 'expect'
                i += 1
                continue
            if mode == 'object':
                objects.append(sys.argv[i])
            elif mode == 'expect':
                expects.append(sys.argv[i])
            else:
                die('bad argument ' + sys.argv[i])
            i += 1
        check(elf, objects, expects)
    else:
        die('unknown subcommand ' + cmd)


main()
