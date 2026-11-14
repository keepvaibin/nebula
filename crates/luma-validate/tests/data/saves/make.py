"""The save corpus (docs/modding/format/validator.md): save files the game's import and the validator are both
run on, with the answer each must give in expected.txt (`<file> <answer>`, the answer one of the game's
NebulaSaveFormat::Error names: None, Short, EncryptedExport, Version, Length, Table, Chunk, Checksum).

Synthetic, as the game's own save test is (runtime/src/save_format_tests.cpp): no owner save bytes. Each of the
three formats (Wii2: version 2 big-endian; Switch2: version 2 little-endian; Native3: version 3 little-endian) has a
good save of one file, a good save of three files with several chunks, and one broken file for each way the check can
fail. Run from anywhere: python crates/luma-validate/tests/data/saves/make.py; the outputs are committed."""
import os

HERE = os.path.dirname(os.path.abspath(__file__))
FORMATS = {"wii2": (2, True), "switch2": (2, False), "native3": (3, False)}


def put32(data, at, value, big):
    data[at:at + 4] = (value & 0xFFFFFFFF).to_bytes(4, "big" if big else "little")


def checksum(body, big):
    total = inverse = 0
    for i in range(0, len(body) - 1, 2):
        word = int.from_bytes(body[i:i + 2], "big" if big else "little")
        total += word
        inverse += (~word) & 0xFFFF
    return (total & 0xFFFF) << 16 | (inverse & 0xFFFF)


def seal(data, big):
    size = int.from_bytes(data[12:16], "big" if big else "little")
    put32(data, 0, checksum(bytes(data[4:size]), big), big)
    return data


def save(version, big, files):
    """files: [(name, [(tag, chunk payload bytes)])]. Each file: byte 0 = 1, byte 1 = chunk count, chunks from 4,
    each 12 bytes of header (tag, a word, its own size) and its payload."""
    entries = len(files)
    table_end = 16 + entries * 16
    blobs = []
    for _, chunks in files:
        blob = bytearray(4)
        blob[0] = 1
        blob[1] = len(chunks)
        for tag, payload in chunks:
            head = bytearray(12)
            put32(head, 0, tag, big)
            put32(head, 4, 0x12345678, big)
            put32(head, 8, 12 + len(payload), big)
            blob += head + payload
        blobs.append(blob)
    size = table_end + sum(len(b) for b in blobs)
    padded = (size + 31) & ~31
    data = bytearray(padded)
    put32(data, 4, version, big)
    put32(data, 8, entries, big)
    put32(data, 12, size, big)
    at = table_end
    for i, ((name, _), blob) in enumerate(zip(files, blobs)):
        entry = 16 + i * 16
        data[entry:entry + len(name)] = name.encode("ascii")
        put32(data, entry + 12, at, big)
        data[at:at + len(blob)] = blob
        at += len(blob)
    return seal(data, big)


def one(version, big):
    return save(version, big, [("mario1", [(0x504C4159, bytes([7]) + bytes(7))])])


def three(version, big):
    return save(version, big, [
        ("mario1", [(0x504C4159, bytes(range(20))), (0x53544152, bytes(36))]),
        ("luigi1", [(0x504C4159, bytes(8))]),
        ("config", [(0x434F4E46, bytes(16)), (0x534F554E, bytes(4)), (0x4D495343, bytes(12))]),
    ])


def cases(version, big):
    """(name, bytes, answer) for one format."""
    good = one(version, big)
    out = [("good-one", good, "None"), ("good-three", three(version, big), "None")]

    def changed(edit, reseal=True):
        data = bytearray(good)
        edit(data)
        return seal(data, big) if reseal else data

    out += [
        ("short-0", bytes(), "Short"),
        ("short-15", bytes(good[:15]), "Short"),
        ("encrypted", bytes(111360), "EncryptedExport"),
        ("version-4", changed(lambda d: put32(d, 4, 4, big), reseal=False), "Version"),
        ("length-padding", bytes(good) + bytes(32), "Length"),
        ("length-cut", bytes(good[:-32]) if len(good) > 32 else bytes(good[:16]), "Length"),
        ("length-under-16", changed(lambda d: put32(d, 12, 15, big), reseal=False), "Length"),
        ("table-no-entries", changed(lambda d: put32(d, 8, 0, big)), "Table"),
        ("table-24-entries", changed(lambda d: put32(d, 8, 24, big)), "Table"),
        ("table-slash-name", changed(lambda d: d.__setitem__(16, ord("/"))), "Table"),
        ("table-empty-name", changed(lambda d: d.__setitem__(slice(16, 28), bytes(12))), "Table"),
        ("table-long-name", changed(lambda d: d.__setitem__(slice(16, 28), b"abcdefghijkl")), "Table"),
        ("table-start-in-table", changed(lambda d: put32(d, 28, 16, big)), "Table"),
        ("table-start-past-end", changed(lambda d: put32(d, 28, 0xFFFFFFF0, big)), "Table"),
        ("chunk-file-version", changed(lambda d: d.__setitem__(32, 2)), "Chunk"),
        ("chunk-size-under-12", changed(lambda d: put32(d, 44, 0, big)), "Chunk"),
        ("chunk-size-over", changed(lambda d: put32(d, 44, 0x1000, big)), "Chunk"),
        ("chunk-count-over", changed(lambda d: d.__setitem__(33, 9)), "Chunk"),
        ("checksum-byte", changed(lambda d: d.__setitem__(48, d[48] ^ 1), reseal=False), "Checksum"),
        ("checksum-word", changed(lambda d: put32(d, 0, 0, big), reseal=False), "Checksum"),
    ]
    return out


def main():
    lines = []
    for fmt, (version, big) in FORMATS.items():
        for name, data, answer in cases(version, big):
            file = f"{fmt}-{name}.bin"
            with open(os.path.join(HERE, file), "wb") as f:
                f.write(bytes(data))
            lines.append(f"{file} {answer}")
    with open(os.path.join(HERE, "expected.txt"), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    print(len(lines), "files")


if __name__ == "__main__":
    main()
