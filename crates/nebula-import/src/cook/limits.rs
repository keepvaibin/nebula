//! Values shared by native code, shaders and the importer.
const HEADER: &[u8] = include_bytes!("../../../../runtime/src/render/materials/nebula_limits.hlsli");

// The value of `#define NEBULA_<name> <number>` in the header; a name only mentioned elsewhere does not count.
const fn definition(name: &[u8]) -> u32 {
    const PREFIX: &[u8] = b"#define NEBULA_";
    let mut i = 0;
    while i + PREFIX.len() + name.len() < HEADER.len() {
        let mut k = 0;
        while k < PREFIX.len() && HEADER[i + k] == PREFIX[k] { k += 1; }
        let mut n = 0;
        while k == PREFIX.len() && n < name.len() && HEADER[i + k + n] == name[n] { n += 1; }
        if k == PREFIX.len() && n == name.len() && HEADER[i + k + n] == b' ' {
            i += k + n + 1;
            let mut radix = 10;
            if HEADER[i] == b'0' && HEADER[i + 1] == b'x' { radix = 16; i += 2; }
            let mut value = 0;
            while i < HEADER.len() {
                let c = HEADER[i];
                let digit = if c >= b'0' && c <= b'9' { (c - b'0') as u32 }
                    else if c >= b'A' && c <= b'F' { (c - b'A' + 10) as u32 }
                    else { return value; };
                value = value * radix + digit;
                i += 1;
            }
            return value;
        }
        i += 1;
    }
    panic!("missing shared definition")
}
pub const NONE: u32 = definition(b"NONE32");
pub const MATERIAL_SLOTS: usize = definition(b"MATERIAL_SLOTS") as usize;
