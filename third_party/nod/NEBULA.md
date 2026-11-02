nod 1.4.4 (https://github.com/encounter/nod-rs, MIT OR Apache-2.0), with one change: src/disc/wii.rs holds no key. A program sets
the retail common key with `set_common_key` before it opens a Wii disc image.
