use std::{
    cmp::min,
    fs::File,
    io,
    io::{BufReader, Read, Seek, SeekFrom},
    path::{Path, PathBuf},
};

use crate::{ErrorContext, Result, ResultContext};

#[derive(Debug)]
pub struct SplitFileReader {
    files: Vec<Split<PathBuf>>,
    open_file: Option<Split<BufReader<File>>>,
    pos: u64,
}

#[derive(Debug, Clone)]
struct Split<T> {
    inner: T,
    begin: u64,
    size: u64,
}

impl<T> Split<T> {
    fn contains(&self, pos: u64) -> bool { self.begin <= pos && pos < self.begin + self.size }
}

// .iso.1, .iso.2, etc.
fn split_path_1(input: &Path, index: u32) -> PathBuf {
    let input_str = input.to_str().unwrap_or("[INVALID]");
    let mut out = input_str.to_string();
    out.push('.');
    out.push(char::from_digit(index, 10).unwrap());
    PathBuf::from(out)
}

// .part1.iso, .part2.iso, etc.
fn split_path_2(input: &Path, index: u32) -> PathBuf {
    let extension = input.extension().and_then(|s| s.to_str()).unwrap_or("iso");
    let input_without_ext = input.with_extension("");
    let input_str = input_without_ext.to_str().unwrap_or("[INVALID]");
    let mut out = input_str.to_string();
    out.push_str(".part");
    out.push(char::from_digit(index, 10).unwrap());
    out.push('.');
    out.push_str(extension);
    PathBuf::from(out)
}

// .wbf1, .wbf2, etc.
fn split_path_3(input: &Path, index: u32) -> PathBuf {
    let input_str = input.to_str().unwrap_or("[INVALID]");
    let mut chars = input_str.chars();
    chars.next_back();
    let mut out = chars.as_str().to_string();
    out.push(char::from_digit(index, 10).unwrap());
    PathBuf::from(out)
}

impl SplitFileReader {
    pub fn empty() -> Self { Self { files: Vec::new(), open_file: None, pos: 0 } }

    pub fn new(path: &Path) -> Result<Self> {
        let mut files = vec![];
        let mut begin = 0;
        match path.metadata() {
            Ok(metadata) => {
                files.push(Split { inner: path.to_path_buf(), begin, size: metadata.len() });
                begin += metadata.len();
            }
            Err(e) => {
                return Err(e.context(format!("Failed to stat file {}", path.display())));
            }
        }
        for path_fn in [split_path_1, split_path_2, split_path_3] {
            let mut index = 1;
            loop {
                let path = path_fn(path, index);
                if let Ok(metadata) = path.metadata() {
                    files.push(Split { inner: path, begin, size: metadata.len() });
                    begin += metadata.len();
                    index += 1;
                } else {
                    break;
                }
            }
            if index > 1 {
                break;
            }
        }
        Ok(Self { files, open_file: None, pos: 0 })
    }

    pub fn add(&mut self, path: &Path) -> Result<()> {
        let begin = self.len();
        let metadata =
            path.metadata().context(format!("Failed to stat file {}", path.display()))?;
        self.files.push(Split { inner: path.to_path_buf(), begin, size: metadata.len() });
        Ok(())
    }

    pub fn len(&self) -> u64 { self.files.last().map_or(0, |f| f.begin + f.size) }
}

impl Read for SplitFileReader {
    fn read(&mut self, buf: &mut [u8]) -> io::Result<usize> {
        if self.open_file.is_none() || !self.open_file.as_ref().unwrap().contains(self.pos) {
            self.open_file = if let Some(split) = self.files.iter().find(|f| f.contains(self.pos)) {
                let mut file = BufReader::new(File::open(&split.inner)?);
                // log::info!("Opened file {} at pos {}", split.inner.display(), self.pos);
                file.seek(SeekFrom::Start(self.pos - split.begin))?;
                Some(Split { inner: file, begin: split.begin, size: split.size })
            } else {
                None
            };
        }
        let Some(split) = self.open_file.as_mut() else {
            return Ok(0);
        };
        let to_read = min(buf.len(), (split.begin + split.size - self.pos) as usize);
        let read = split.inner.read(&mut buf[..to_read])?;
        self.pos += read as u64;
        Ok(read)
    }
}

impl Seek for SplitFileReader {
    fn seek(&mut self, pos: SeekFrom) -> io::Result<u64> {
        self.pos = match pos {
            SeekFrom::Start(pos) => pos,
            SeekFrom::Current(offset) => self.pos.saturating_add_signed(offset),
            SeekFrom::End(offset) => self.len().saturating_add_signed(offset),
        };
        if let Some(split) = &mut self.open_file {
            if split.contains(self.pos) {
                // Seek within the open file
                split.inner.seek(SeekFrom::Start(self.pos - split.begin))?;
            }
        }
        Ok(self.pos)
    }
}

impl Clone for SplitFileReader {
    fn clone(&self) -> Self { Self { files: self.files.clone(), open_file: None, pos: 0 } }
}
