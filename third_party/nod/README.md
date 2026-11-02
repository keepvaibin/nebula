# nod [![Build Status]][actions] [![Latest Version]][crates.io] [![Api Rustdoc]][rustdoc] ![Rust Version]

[Build Status]: https://github.com/encounter/nod/actions/workflows/build.yaml/badge.svg
[actions]: https://github.com/encounter/nod/actions
[Latest Version]: https://img.shields.io/crates/v/nod.svg
[crates.io]: https://crates.io/crates/nod
[Api Rustdoc]: https://img.shields.io/badge/api-rustdoc-blue.svg
[rustdoc]: https://docs.rs/nod
[Rust Version]: https://img.shields.io/badge/rust-1.74+-blue.svg?maxAge=3600

Library for traversing & reading Nintendo Optical Disc (GameCube and Wii) images.

Originally based on the C++ library [nod](https://github.com/AxioDL/nod),
but does not currently support authoring.

Currently supported file formats:

- ISO (GCM)
- WIA / RVZ
- WBFS (+ NKit 2 lossless)
- CISO (+ NKit 2 lossless)
- NFS (Wii U VC)
- GCZ
- TGC

## CLI tool

This crate includes a command-line tool called `nodtool`.

Download the latest release from the [releases page](https://github.com/encounter/nod-rs/releases),
or install it using Cargo:

```shell
cargo install --locked nodtool
```

### info

Displays information about a disc image.

```shell
nodtool info /path/to/game.iso
```

### extract

Extracts the contents of a disc image to a directory.

```shell
nodtool extract /path/to/game.iso [outdir]
```

For Wii U VC titles, use `content/hif_000000.nfs`:

```shell
nodtool extract /path/to/game/content/hif_000000.nfs [outdir]
```

### convert

Converts any supported format to raw ISO.

```shell
nodtool convert /path/to/game.wia /path/to/game.iso
```

### verify

Hashes the contents of a disc image and verifies it.

```shell
nodtool verify /path/to/game.iso
```

## Library example

Opening a disc image and reading a file:

```rust
use std::io::Read;

// Open a disc image and the first data partition.
let disc = nod::Disc::new("path/to/file.iso")
    .expect("Failed to open disc");
let mut partition = disc.open_partition_kind(nod::PartitionKind::Data)
    .expect("Failed to open data partition");

// Read partition metadata and the file system table.
let meta = partition.meta()
    .expect("Failed to read partition metadata");
let fst = meta.fst()
    .expect("File system table is invalid");

// Find a file by path and read it into a string.
if let Some((_, node)) = fst.find("/MP3/Worlds.txt") {
    let mut s = String::new();
    partition
        .open_file(node)
        .expect("Failed to open file stream")
        .read_to_string(&mut s)
        .expect("Failed to read file");
    println!("{}", s);
}
```

Converting a disc image to raw ISO:

```rust
// Enable `rebuild_encryption` to ensure the output is a valid ISO.
let options = nod::OpenOptions { rebuild_encryption: true, ..Default::default() };
let mut disc = nod::Disc::new_with_options("path/to/file.rvz", &options)
    .expect("Failed to open disc");

// Read directly from the open disc and write to the output file.
let mut out = std::fs::File::create("output.iso")
    .expect("Failed to create output file");
std::io::copy(&mut disc, &mut out)
    .expect("Failed to write data");
```

## License

Licensed under either of

- Apache License, Version 2.0, ([LICENSE-APACHE](LICENSE-APACHE) or <http://www.apache.org/licenses/LICENSE-2.0>)
- MIT license ([LICENSE-MIT](LICENSE-MIT) or <http://opensource.org/licenses/MIT>)

at your option.

### Contribution

Unless you explicitly state otherwise, any contribution intentionally submitted
for inclusion in the work by you, as defined in the Apache-2.0 license, shall be dual licensed as above, without any
additional terms or conditions.
