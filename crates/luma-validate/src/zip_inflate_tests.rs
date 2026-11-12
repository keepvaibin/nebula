//! unpack against a reference, for every size around the window and the 64 KiB buffers, every kind of content and every
//! way of packing it (the reader once failed any deflated entry over 65,536 bytes whose packed bytes came in one read).
use super::*;
use miniz_oxide::deflate::compress_to_vec;
use miniz_oxide::deflate::core::CompressorOxide;
use miniz_oxide::deflate::stream::deflate;
use miniz_oxide::{DataFormat, MZFlush};

const SIZES: [usize; 7] = [1, 32_767, 32_768, 65_535, 65_536, 65_537, 1 << 20];
const BIG: [usize; 2] = [16 << 20, 64 << 20];

fn content(kind: usize, size: usize) -> Vec<u8> {
    match kind {
        0 => vec![0; size],
        1 => {
            let words = ["mario ", "galaxy ", "luma ", "star bit ", "launch star ", "\n", "gravity ", "1234 "];
            let mut out = Vec::with_capacity(size);
            let mut i = 0usize;
            while out.len() < size {
                out.extend_from_slice(words[(i * 7 + i / 13) % words.len()].as_bytes());
                i += 1;
            }
            out.truncate(size);
            out
        }
        _ => {
            let mut x = 0x9E37_79B9_7F4A_7C15u64 ^ size as u64;
            (0..size)
                .map(|_| {
                    x ^= x << 13;
                    x ^= x >> 7;
                    x ^= x << 17;
                    (x >> 24) as u8
                })
                .collect()
        }
    }
}

/// Raw deflate in many blocks: a sync flush every `every` bytes (each block ends on a byte boundary, an empty stored
/// block after it), as streaming writers make.
fn deflate_flushed(data: &[u8], level: u8, every: usize) -> Vec<u8> {
    let mut state = Box::<CompressorOxide>::default();
    state.set_format_and_level(DataFormat::Raw, level);
    let mut out = Vec::new();
    let mut buffer = vec![0u8; 1 << 16];
    let mut pieces = data.chunks(every.max(1)).peekable();
    if data.is_empty() {
        let r = deflate(&mut state, &[], &mut buffer, MZFlush::Finish);
        out.extend_from_slice(&buffer[..r.bytes_written]);
        return out;
    }
    while let Some(piece) = pieces.next() {
        let flush = if pieces.peek().is_none() { MZFlush::Finish } else { MZFlush::Sync };
        let mut rest = piece;
        loop {
            let r = deflate(&mut state, rest, &mut buffer, flush);
            out.extend_from_slice(&buffer[..r.bytes_written]);
            rest = &rest[r.bytes_consumed..];
            if rest.is_empty() && r.bytes_written < buffer.len() {
                break;
            }
        }
    }
    out
}

fn entry(packed: &[u8], data: &[u8], method: Method) -> Entry {
    Entry {
        name: Some("code/windows-x64/mod.dll".into()),
        raw_name: b"code/windows-x64/mod.dll".to_vec(),
        kind: EntryKind::File,
        method: Some(method),
        method_found: if method == Method::Deflate { 8 } else { 0 },
        encrypted: false,
        crc: crc32fast::hash(data),
        packed: packed.len() as u64,
        unpacked: data.len() as u64,
        data_at: Some(0),
        local_name_differs: false,
    }
}

fn round_trip(data: &[u8], what: &str) {
    let ways: Vec<(String, Vec<u8>, Method)> = vec![
        ("stored".into(), data.to_vec(), Method::Stored),
        ("level 0 (stored blocks)".into(), compress_to_vec(data, 0), Method::Deflate),
        ("level 1".into(), compress_to_vec(data, 1), Method::Deflate),
        ("level 6".into(), compress_to_vec(data, 6), Method::Deflate),
        ("level 9".into(), compress_to_vec(data, 9), Method::Deflate),
        ("level 6, flushed every 10,000".into(), deflate_flushed(data, 6, 10_000), Method::Deflate),
        ("level 9, flushed every 70,000".into(), deflate_flushed(data, 9, 70_000), Method::Deflate),
    ];
    for (way, packed, method) in ways {
        let e = entry(&packed, data, method);
        let mut bytes: &[u8] = &packed;
        match unpack(&mut bytes, &e, true, data.len() as u64) {
            Unpacked::Done { bytes: Some(got), size, crc_ok } => {
                assert_eq!(size, data.len() as u64, "{what}, {way}: size");
                assert!(crc_ok, "{what}, {way}: crc");
                assert!(got == data, "{what}, {way}: first difference at {:?}", got.iter().zip(data).position(|(a, b)| a != b));
            }
            other => panic!("{what}, {way}: {other:?}"),
        }
        // Counting without keeping finds the same; one byte under the size is over the limit.
        let mut bytes: &[u8] = &packed;
        assert_eq!(unpack(&mut bytes, &e, false, data.len() as u64), Unpacked::Done { bytes: None, size: data.len() as u64, crc_ok: true }, "{what}, {way}");
        if !data.is_empty() {
            let mut bytes: &[u8] = &packed;
            assert_eq!(unpack(&mut bytes, &e, false, data.len() as u64 - 1), Unpacked::OverLimit, "{what}, {way}");
        }
    }
}

#[test]
fn every_size_kind_and_packing_unpacks_to_its_bytes() {
    for size in SIZES {
        for kind in 0..3 {
            round_trip(&content(kind, size), &format!("{size} bytes of kind {kind}"));
        }
    }
}

/// 16 and 64 MiB: cargo test -p luma-validate --lib big_entries -- --ignored (slow in a debug build).
#[test]
#[ignore = "slow: 16 and 64 MiB entries"]
fn big_entries_unpack_to_their_bytes() {
    for size in BIG {
        for kind in 0..3 {
            round_trip(&content(kind, size), &format!("{size} bytes of kind {kind}"));
        }
    }
}

#[test]
fn a_cut_or_damaged_stream_is_broken_or_its_crc_differs() {
    let data = content(1, 200_000);
    let packed = compress_to_vec(&data, 6);
    // Cut anywhere: never a hang, never Done with the right CRC.
    for cut in [1, packed.len() / 3, packed.len() / 2, packed.len() - 1] {
        let short = &packed[..cut];
        let mut e = entry(short, &data, Method::Deflate);
        e.packed = cut as u64;
        let mut bytes: &[u8] = short;
        let got = unpack(&mut bytes, &e, false, data.len() as u64);
        assert!(!matches!(got, Unpacked::Done { crc_ok: true, .. }), "cut at {cut}: {got:?}");
    }
    // A flipped byte: broken, or a different CRC.
    let mut bad = packed.clone();
    let middle = bad.len() / 2;
    bad[middle] ^= 0x5A;
    let e = entry(&bad, &data, Method::Deflate);
    let mut bytes: &[u8] = &bad;
    assert!(!matches!(unpack(&mut bytes, &e, false, u64::MAX), Unpacked::Done { crc_ok: true, size: 200_000, .. }));
    // Bytes after the stream's end are not the entry's.
    let mut trailing = packed.clone();
    trailing.extend_from_slice(&[0xFF; 100]);
    let e = entry(&trailing, &data, Method::Deflate);
    let mut bytes: &[u8] = &trailing;
    assert_eq!(unpack(&mut bytes, &e, false, u64::MAX), Unpacked::Done { bytes: None, size: 200_000, crc_ok: true });
    // An empty entry.
    round_trip(&[], "empty");
}
