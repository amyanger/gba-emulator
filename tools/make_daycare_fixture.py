#!/usr/bin/env python3
"""Write a test save with two Daycare Pokemon, for checking the Daycare block.

The party mon in slot 2 is copied into both Daycare slots (slot 1 gets a new
PID and OT ID). Party slots can be turned into eggs, and a waiting egg can be
faked. Reads IN, writes OUT, never edits in place. Dev tool only.

  make_daycare_fixture.py IN.sav OUT.sav --steps1 246 --counter 250 \
      --eggs 4:2 5:3 [--offspring 67D7E458] [--fire-slot 0] [--steps0 N]
"""
import argparse, os, struct, sys

PERM = ["GAEM", "GAME", "GEAM", "GEMA", "GMAE", "GMEA", "AGEM", "AGME", "AEGM", "AEMG", "AMGE", "AMEG",
        "EGAM", "EGMA", "EAGM", "EAMG", "EMGA", "EMAG", "MGAE", "MGEA", "MAGE", "MAEG", "MEGA", "MEAG"]
SIZES = {0: 0xF2C, 1: 0xF80, 2: 0xF80, 3: 0xF80, 4: 0xF08}
SECTOR_SIG = 0x08012025
DAYCARE = 0x3030
MON_SIZE = 0x8C
STEPS = 0x88
OFFSPRING = 0x118
STEP_COUNTER = 0x11C
FLAGS = 0x1270
FLAG_PENDING_DAYCARE_EGG = 0x86
SLUGMA = 218


def party_off(i):
    return 0x238 + i * 100


def decrypt(b):
    pid, otid = struct.unpack_from('<II', b, 0)
    key = pid ^ otid
    data = bytearray(b[32:80])
    for i in range(0, 48, 4):
        struct.pack_into('<I', data, i, struct.unpack_from('<I', data, i)[0] ^ key)
    o = PERM[pid % 24]
    return {c: bytearray(data[o.index(c) * 12:o.index(c) * 12 + 12]) for c in 'GAEM'}


def encrypt(b, sub, pid, otid):
    b = bytearray(b)
    struct.pack_into('<II', b, 0, pid, otid)
    data = bytearray(b''.join(sub[c] for c in PERM[pid % 24]))
    struct.pack_into('<H', b, 0x1C, sum(struct.unpack('<24H', data)) & 0xFFFF)
    key = pid ^ otid
    for i in range(0, 48, 4):
        struct.pack_into('<I', data, i, struct.unpack_from('<I', data, i)[0] ^ key)
    b[32:80] = data
    return bytes(b)


def modify(b, pid=None, otid=None, species=None, cycles=None, egg=None):
    p0, o0 = struct.unpack_from('<II', b, 0)
    sub = decrypt(b)
    if species is not None:
        struct.pack_into('<H', sub['G'], 0, species)
    if cycles is not None:
        sub['G'][9] = cycles
    b = bytearray(b)
    if egg is not None:
        w = struct.unpack_from('<I', sub['M'], 4)[0]
        w = (w | (1 << 30)) if egg else (w & ~(1 << 30))
        struct.pack_into('<I', sub['M'], 4, w)
        b[19] = (b[19] | 4) if egg else (b[19] & ~4)
    return encrypt(b, sub, p0 if pid is None else pid, o0 if otid is None else otid)


def latest_sections(d):
    best = None
    for slot in (0, 1):
        secs, cnt = {}, 0
        for i in range(14):
            o = slot * 0xE000 + i * 0x1000
            sid, _, sig, c = struct.unpack_from('<HHII', d, o + 0xFF4)
            if sig == SECTOR_SIG:
                secs[sid] = o
                cnt = c
        if secs and (best is None or cnt > best[0]):
            best = (cnt, secs)
    if best is None:
        sys.exit('no valid save slot found')
    return best[1]


def hexint(s):
    return int(s, 16)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('infile')
    ap.add_argument('outfile')
    ap.add_argument('--steps0', type=int, default=0, help='slot 0 Daycare steps')
    ap.add_argument('--steps1', type=int, default=0xF0, help='slot 1 Daycare steps (the egg roll uses this one)')
    ap.add_argument('--counter', type=int, default=250, help='hatch step counter (0-255)')
    ap.add_argument('--offspring', type=hexint, help='waiting egg PID in hex; also sets the pending egg flag')
    ap.add_argument('--eggs', nargs='*', default=[], metavar='SLOT:CYCLES', help='turn party slots into eggs')
    ap.add_argument('--fire-slot', type=int, help='make this party slot a Slugma (Flame Body)')
    ap.add_argument('--pid1', type=hexint, default=0x12345690, help='slot 1 PID in hex')
    ap.add_argument('--otx', type=hexint, default=0x10000, help='XOR applied to slot 1 OT ID, in hex')
    a = ap.parse_args()

    inp, out = os.path.realpath(a.infile), os.path.realpath(a.outfile)
    if inp == out:
        sys.exit('OUT must differ from IN')
    if 'roms' in (part.lower() for part in out.split(os.sep)[:-1]):
        sys.exit('refusing to write inside a roms directory')

    d = bytearray(open(a.infile, 'rb').read())
    secs = latest_sections(d)

    def sb1(off):
        return secs[1 + off // 0xF80] + off % 0xF80

    def rd(off, n):
        return bytes(d[sb1(off):sb1(off) + n])

    def wr(off, bs):
        o = sb1(off)
        d[o:o + len(bs)] = bs

    parent = rd(party_off(2), 80)
    wr(DAYCARE, modify(parent))
    wr(DAYCARE + STEPS, struct.pack('<I', a.steps0))
    otid = struct.unpack_from('<I', parent, 4)[0]
    wr(DAYCARE + MON_SIZE, modify(parent, pid=a.pid1, otid=otid ^ a.otx))
    wr(DAYCARE + MON_SIZE + STEPS, struct.pack('<I', a.steps1))
    wr(DAYCARE + OFFSPRING, struct.pack('<I', a.offspring or 0))
    wr(DAYCARE + STEP_COUNTER, bytes([a.counter & 0xFF]))

    party_count = rd(0x234, 1)[0]
    for spec in a.eggs:
        try:
            slot, cycles = (int(x) for x in spec.split(':'))
        except ValueError:
            sys.exit('bad --eggs entry %r, expected SLOT:CYCLES' % spec)
        if not 0 <= slot < min(party_count, 6):
            sys.exit('--eggs slot %d is outside the party (%d Pokemon)' % (slot, party_count))
        if not 0 <= cycles <= 255:
            sys.exit('--eggs cycles %d must be 0 to 255' % cycles)
        wr(party_off(slot), modify(rd(party_off(slot), 80), cycles=cycles, egg=True))
    if a.fire_slot is not None:
        if not 0 <= a.fire_slot < min(party_count, 6):
            sys.exit('--fire-slot %d is outside the party (%d Pokemon)' % (a.fire_slot, party_count))
        wr(party_off(a.fire_slot), modify(rd(party_off(a.fire_slot), 80), species=SLUGMA))
    if a.offspring:
        flag_byte = FLAGS + FLAG_PENDING_DAYCARE_EGG // 8
        wr(flag_byte, bytes([rd(flag_byte, 1)[0] | (1 << (FLAG_PENDING_DAYCARE_EGG % 8))]))

    for sid, o in secs.items():
        if sid in SIZES:
            s = sum(struct.unpack_from('<%dI' % (SIZES[sid] // 4), d, o)) & 0xFFFFFFFF
            struct.pack_into('<H', d, o + 0xFF6, ((s >> 16) + s) & 0xFFFF)
    open(a.outfile, 'wb').write(d)


if __name__ == '__main__':
    main()
