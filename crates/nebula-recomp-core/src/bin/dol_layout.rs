use nebula_recomp_core::DolImage;
use std::fs;
fn main() {
    let path = std::env::args().nth(1).expect("dol path");
    let data = fs::read(&path).expect("read");
    let dol = DolImage::parse(&data).expect("parse");
    println!("entry 0x{:08X} bss 0x{:08X}+0x{:X}", dol.entry_point, dol.bss_address, dol.bss_size);
    for s in &dol.sections {
        println!("{:>5} {:>3} off=0x{:08X} va=0x{:08X} size=0x{:08X} end=0x{:08X}", format!("{:?}", s.kind), s.index, s.file_offset, s.address, s.size, s.address + s.size);
    }
}
