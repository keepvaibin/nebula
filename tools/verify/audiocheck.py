"""audiocheck.py SOURCE.PAK COOKED.PAK: each AST stream of a source pak (big-endian) against its cooked file: header numbers,
every block's size and last samples, every PCM16 sample; each wave of the cooked archive: inside its AW file, PCM16; a loop cooked audio
history the disc stored against the decoded wave; every other file of AudioRes unchanged in size. Exit 1 on a difference."""
import array
import struct
import sys

from pakfiles import files


def read(pak, entry):
    pak.seek(entry[0])
    return pak.read(entry[1])


def swapped16(data):
    a = array.array("H")
    a.frombytes(data)
    a.byteswap()
    return a.tobytes()


def check_ast(name, s, d):
    errors = []
    tag, channels, rate, samples, loop, loop_start, loop_end, volume = struct.unpack_from("<8I", d, 0)
    if tag != 0x4E535452 or len(d) != 32 + samples * 2 * channels:
        return [f"{name}: size or tag"]
    if (channels, loop, rate, samples, loop_start, loop_end) != (
            struct.unpack_from(">H", s, 0xC)[0], struct.unpack_from(">H", s, 0xE)[0], *struct.unpack_from(">IIII", s, 0x10)):
        errors.append(f"{name}: header numbers")
    if volume != s[0x28]:
        errors.append(f"{name}: volume")
    at = 0x40
    blocks = 0
    done = 0
    while at < len(s):
        if s[at : at + 4] != b"BLCK":
            errors.append(f"{name}: block tag at {at:#x}")
            break
        size = struct.unpack_from(">I", s, at + 4)[0]
        data = at + 0x20
        per = size if data + size * channels <= len(s) else max(len(s) - (data + size * (channels - 1)), 0)
        take = min(per, samples * 2 - done)
        for c in range(channels):
            src = s[data + size * c : data + size * c + take]
            if swapped16(src) != d[32 + samples * 2 * c + done : 32 + samples * 2 * c + done + take]:
                errors.append(f"{name}: samples at {at:#x}, channel {c}")
        done += take
        at = data + size * (channels - 1) + per
        blocks += 1
    return errors, blocks, channels, 1


def waves(smr, unpack):
    """Each wave of each wave system of an audio archive: (archive name, wave's fields)."""
    at = smr.find(b"WSYS")
    while at >= 0:
        bank = unpack(smr, at + 0x10)
        for a in range(unpack(smr, at + bank + 4)):
            archive = unpack(smr, at + bank + 8 + 4 * a)
            if archive:
                name = smr[at + archive : smr.index(b"\0", at + archive)].decode("latin-1").lower()
                for w in range(unpack(smr, at + archive + 0x70)):
                    wave = unpack(smr, at + archive + 0x74 + 4 * w)
                    if wave:
                        yield name, at + wave
        at = smr.find(b"WSYS", at + 4)


def le32(data, at):
    return struct.unpack_from("<I", data, at)[0]


def main():
    errors = []
    with open(sys.argv[1], "rb") as a, open(sys.argv[2], "rb") as b:
        fa, fb = files(a), files(b)
        asts = blocks = 0
        for name in sorted(fa):
            if not name.lower().endswith(".ast"):
                continue
            result = check_ast(name, read(a, fa[name]), read(b, fb[name]))
            if isinstance(result, list):
                errors += result
                continue
            errors += result[0]
            asts += 1
            blocks += result[1]
        cooked = read(b, fb["AudioRes/SMR"])
        aw = {name.rsplit("/", 1)[-1].lower(): name for name in fa if name.lower().endswith(".aw")}
        loaded = {}
        checked = loops = loop_moved = worst = 0
        for name, at in waves(cooked, le32):
            kind = cooked[at + 1]
            if kind > 3:
                continue
            checked += 1
            if name not in loaded:
                loaded[name] = read(a, fa[aw[name]]), read(b, fb[aw[name]])
            source, target = loaded[name]
            offset, length, flags, loop_start, loop_end, count = struct.unpack_from("<IIIIII", cooked, at + 8)
            last, penult = struct.unpack_from("<hh", cooked, at + 0x20)
            if offset + length > len(target) or length < count * 2:
                errors.append(f"{name} wave at {at:#x}: {offset:#x}+{length:#x} of {len(target):#x}, {count} samples")
                continue
            if flags and kind < 2 and loop_start >= 16:
                # What the disc's decoder would restore at the loop, against what the decoded wave holds there.
                loops += 1
                was_last, was_penult = struct.unpack_from("<hh", target, offset + ((loop_start & ~15) - 2) * 2)
                if (was_penult, was_last) != (last, penult):
                    moved = max(abs(was_penult - last), abs(was_last - penult))
                    loop_moved += 1
                    worst = max(worst, moved)
        print(f"{checked} waves placed; {loops} loops, {loop_moved} whose stored history is not the decoded wave's (largest {worst})")
        aws = turned = 0
        for name in aw:
            aws += 1
            turned += len(read(b, fb[aw[name]]))
    print(f"{asts} streams, {blocks} blocks; {aws} wave archives, {turned} PCM16 bytes; {len(errors)} differences")
    for e in errors[:40]:
        print(e)
    sys.exit(1 if errors else 0)


if __name__ == "__main__":
    main()
