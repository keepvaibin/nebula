"""moviecheck.py SOURCE.PAK COOKED.PAK: every movie of a source pak (THP, big-endian) against its cooked file (NMOV, host
order), every frame: the header's fields, each frame's next size, the JPEG bytes, each track's audio (first samples of each channel decoded and compared),
the cooked frames' sizes and 32-byte padding, and the file's end. Exit 1 on any difference."""
import struct
import sys

import pakfiles

HEADER = 0x40
FRAME_HEADER = 0x10
AUDIO_HEADER = 0x50


def files(pak):
    return {path.rsplit("/", 1)[-1]: at for path, at in pakfiles.files(pak).items() if path.lower().endswith(".thp")}


def decoded(record, channel, count):
    """The first `count` samples of a disc audio record's channel (DSP-ADPCM)."""
    second, _ = struct.unpack_from(">II", record, 0)
    coefficients = struct.unpack_from(">16h", record, 8 + 32 * channel)
    h1, h2 = struct.unpack_from(">2h", record, 0x48 + 4 * channel)
    data = AUDIO_HEADER + (second if channel else 0)
    out = []
    for i in range(count):
        at = data + i // 14 * 8
        if at + 8 > len(record):
            out.append(0)
            continue
        c1, c2 = coefficients[((record[at] >> 4) & 7) * 2 : ((record[at] >> 4) & 7) * 2 + 2]
        scale = record[at] & 15
        byte = record[at + 1 + (i % 14) // 2]
        bits = (byte << 4) & 0xFF if i % 14 & 1 else byte
        nibble = ((bits ^ 0x80) - 0x80) >> 4
        value = c1 * h1 + c2 * h2 + ((nibble << scale) << 11)
        value = max(-2147483648, min(2147483647, (value << 5) + 0x8000)) >> 16
        h2, h1 = h1, value
        out.append(value)
    return out


def check(name, src, dst):
    errors = []
    frame_rate = src[0x10:0x14][::-1]
    frames, size = struct.unpack_from(">II", src, 0x14)
    info = struct.unpack_from(">I", src, 0x20)[0]
    at = struct.unpack_from(">I", src, 0x28)[0]
    components = struct.unpack_from(">I", src, info)[0]
    width, height = struct.unpack_from(">II", src, info + 20)
    tracks = struct.unpack_from(">I", src, info + 44)[0] if components > 1 else 0
    if dst[:4] != b"NMOV":
        return [f"{name}: magic {dst[:4]}"]
    h = struct.unpack_from("<II", dst, 4)
    first = struct.unpack_from("<I", dst, 16)[0]
    if h[0] != frames or dst[8:12] != frame_rate:
        errors.append(f"{name}: frames or rate")
    if struct.unpack_from("<III", dst, 20) != (width, height, tracks):
        errors.append(f"{name}: width, height or tracks")
    if any(dst[32:HEADER]):
        errors.append(f"{name}: reserved bytes")
    out = HEADER
    largest = 0
    next_size = first
    sizes = []
    for f in range(frames):
        if next_size == 0 or next_size % 32:
            errors.append(f"{name} frame {f}: size {next_size}")
            break
        sizes.append(next_size)
        body = src[at : at + size]
        video = struct.unpack_from(">I", body, 8)[0]
        audio = struct.unpack_from(">I", body, 12)[0] if components > 1 else 0
        payload = 8 + components * 4
        n, v, a, zero = struct.unpack_from("<IIII", dst, out)
        samples = max((struct.unpack_from(">I", body, payload + video + audio * k + 4)[0] for k in range(tracks)), default=0)
        pcm = 4 + samples * 4 if tracks else 0
        if v != video or a != pcm or zero != 0:
            errors.append(f"{name} frame {f}: sizes {v},{a},{zero} vs {video},{pcm}")
        if dst[out + FRAME_HEADER : out + FRAME_HEADER + video] != body[payload : payload + video]:
            errors.append(f"{name} frame {f}: video bytes")
        for t in range(tracks):
            record = body[payload + video + audio * t : payload + video + audio * (t + 1)]
            d = out + FRAME_HEADER + video + pcm * t
            if struct.unpack_from("<I", dst, d)[0] != samples:
                errors.append(f"{name} frame {f} track {t}: samples")
            second, own = struct.unpack_from(">II", record, 0)
            # The first samples of each channel; decoding every frame in full takes hours here.
            count = min(own, 56)
            cooked = struct.unpack_from(f"<{count * 2}h", dst, d + 4)
            left = decoded(record, 0, count)
            right = decoded(record, 1, count) if second else left
            if list(cooked[0::2]) != left or list(cooked[1::2]) != right:
                errors.append(f"{name} frame {f} track {t}: samples differ")
        used = FRAME_HEADER + video + pcm * tracks
        if any(dst[out + used : out + next_size]):
            errors.append(f"{name} frame {f}: padding")
        largest = max(largest, next_size)
        out += next_size
        at += size
        size = struct.unpack_from(">I", body, 0)[0]
        next_size = n
    if next_size != first and not errors:
        errors.append(f"{name}: the last frame's next size {next_size}, not the first's {first}")
    if struct.unpack_from("<I", dst, 12)[0] != largest:
        errors.append(f"{name}: largest frame")
    if out != len(dst):
        errors.append(f"{name}: ends at {out}, file is {len(dst)}")
    print(f"{name}: {frames} frames, {width}x{height}, {tracks} tracks, largest {largest}, {len(errors)} differences")
    return errors


def main():
    with open(sys.argv[1], "rb") as a, open(sys.argv[2], "rb") as b:
        fa, fb = files(a), files(b)
        errors = []
        if sorted(fa) != sorted(fb):
            errors.append("the movies' names differ")
        for name in sorted(fa):
            a.seek(fa[name][0])
            b.seek(fb[name][0])
            errors += check(name, a.read(fa[name][1]), b.read(fb[name][1]))
    for e in errors[:40]:
        print(e)
    print(f"{len(fa)} movies, {len(errors)} differences")
    sys.exit(1 if errors else 0)


if __name__ == "__main__":
    main()
