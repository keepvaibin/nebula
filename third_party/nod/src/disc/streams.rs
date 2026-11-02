//! Partition file read stream.

use std::{
    io,
    io::{BufRead, Read, Seek, SeekFrom},
};

use super::PartitionBase;

/// A file read stream borrowing a [`PartitionBase`].
pub type FileStream<'a> = WindowedStream<&'a mut dyn PartitionBase>;

/// A file read stream owning a [`PartitionBase`].
pub type OwnedFileStream = WindowedStream<Box<dyn PartitionBase>>;

/// A read stream with a fixed window.
#[derive(Clone)]
pub struct WindowedStream<T>
where T: BufRead + Seek
{
    base: T,
    pos: u64,
    begin: u64,
    end: u64,
}

impl<T> WindowedStream<T>
where T: BufRead + Seek
{
    /// Creates a new windowed stream with offset and size.
    ///
    /// Seeks underlying stream immediately.
    #[inline]
    pub fn new(mut base: T, offset: u64, size: u64) -> io::Result<Self> {
        base.seek(SeekFrom::Start(offset))?;
        Ok(Self { base, pos: offset, begin: offset, end: offset + size })
    }

    /// Returns the length of the window.
    #[inline]
    #[allow(clippy::len_without_is_empty)]
    pub fn len(&self) -> u64 { self.end - self.begin }
}

impl<T> Read for WindowedStream<T>
where T: BufRead + Seek
{
    #[inline]
    fn read(&mut self, out: &mut [u8]) -> io::Result<usize> {
        let buf = self.fill_buf()?;
        let len = buf.len().min(out.len());
        out[..len].copy_from_slice(&buf[..len]);
        self.consume(len);
        Ok(len)
    }
}

impl<T> BufRead for WindowedStream<T>
where T: BufRead + Seek
{
    #[inline]
    fn fill_buf(&mut self) -> io::Result<&[u8]> {
        let limit = self.end.saturating_sub(self.pos);
        if limit == 0 {
            return Ok(&[]);
        }
        let buf = self.base.fill_buf()?;
        let max = (buf.len() as u64).min(limit) as usize;
        Ok(&buf[..max])
    }

    #[inline]
    fn consume(&mut self, amt: usize) {
        self.base.consume(amt);
        self.pos += amt as u64;
    }
}

impl<T> Seek for WindowedStream<T>
where T: BufRead + Seek
{
    #[inline]
    fn seek(&mut self, pos: SeekFrom) -> io::Result<u64> {
        let mut pos = match pos {
            SeekFrom::Start(p) => self.begin + p,
            SeekFrom::End(p) => self.end.saturating_add_signed(p),
            SeekFrom::Current(p) => self.pos.saturating_add_signed(p),
        };
        if pos < self.begin {
            pos = self.begin;
        } else if pos > self.end {
            pos = self.end;
        }
        let result = self.base.seek(SeekFrom::Start(pos))?;
        self.pos = result;
        Ok(result - self.begin)
    }

    #[inline]
    fn stream_position(&mut self) -> io::Result<u64> { Ok(self.pos) }
}
